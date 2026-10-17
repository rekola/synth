#ifndef _TRACKERODE_H_
#define _TRACKERODE_H_

#include "../audio/AudioBuffer.h"
#include "../dsp/NoiseGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Erode (PerformanceEffect::ERODE): a track's output broken into large,
// patchy grains. The track is recorded continuously while held, and grains
// of 40-250 ms are played back from up to 300 ms in the past at random
// moments, each at its own level, a few cents off pitch and turned a little
// around the listener. Between grains there is silence, so the harder the
// pad is pressed - the fewer grains - the more of the sound is missing.
// Every channel the track sends is recorded, so the fragments stay where the
// track sits (give or take the turn) and feed the send bus the same way.
class TrackErode {
public:
  static constexpr int kMaxRegularChannels = 16; // third order
  static constexpr int kMaxGrains = 12;
  static constexpr float kMinGrainSeconds = 0.04f, kMaxGrainSeconds = 0.25f;
  static constexpr float kMaxLookBackSeconds = 0.3f;
  static constexpr float kMaxDetuneCents = 8.0f;
  static constexpr float kMaxTurnDegrees = 30.0f;
  static constexpr float kFadeSeconds = 0.02f;

  explicit TrackErode(uint32_t seed = 1) : random_(seed) { }

  // Starts eroding the track at `amount` (0-1), fading in.
  void start(int sample_rate, int regular_channels, float amount) {
    sample_rate_ = sample_rate;
    regular_ = std::min(regular_channels, kMaxRegularChannels);
    order_ = static_cast<int>(std::lround(std::sqrt(static_cast<double>(std::max(regular_, 1))))) - 1;
    capacity_ = static_cast<int>(static_cast<float>(sample_rate) * (kMaxGrainSeconds * 1.1f + kMaxLookBackSeconds)) + 16;
    size_t needed = static_cast<size_t>(regular_ + 2) * static_cast<size_t>(capacity_);
    if (ring_.size() < needed) ring_.resize(needed);
    std::fill(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(needed), 0.0f);
    written_ = 0;
    for (auto & g : grains_) g.active = false;
    has_main_ = has_aux_a_ = has_aux_b_ = false;
    mix_ = 0.0f;
    releasing_ = false;
    active_ = true;
    setAmount(amount);
  }

  // How eroded: from overlapping grains (0) to scattered fragments (1).
  void setAmount(float amount) {
    float a = std::clamp(amount, 0.0f, 1.0f);
    const float mean_grain_frames = 0.5f * (kMinGrainSeconds + kMaxGrainSeconds) * static_cast<float>(sample_rate_);
    // Grains sounding at once on average: 1.6 is nearly continuous, 0.3
    // leaves the sound mostly gaps.
    const float overlap = 1.6f - 1.3f * a;
    spawn_chance_ = overlap / mean_grain_frames;
  }

  void release() { releasing_ = true; }
  bool isOff() const { return !active_; }

  void process(AudioBuffer & data) {
    if (!active_) return;
    const int frames = data.numberOfFrames();
    const int planes = regular_ + 2;
    const bool in_main = data.hasChannel(Channel::Main) && data.regularChannelCount() == regular_;
    std::array<const float *, kMaxRegularChannels + 2> in {};
    if (in_main) {
      for (int c = 0; c < regular_; c++) in[static_cast<size_t>(c)] = data.getChannelData(c);
    }
    in[static_cast<size_t>(regular_)] = data.getChannel(Channel::AuxA);
    in[static_cast<size_t>(regular_ + 1)] = data.getChannel(Channel::AuxB);
    has_main_ = has_main_ || in_main;
    has_aux_a_ = has_aux_a_ || in[static_cast<size_t>(regular_)];
    has_aux_b_ = has_aux_b_ || in[static_cast<size_t>(regular_ + 1)];

    AudioBuffer out(has_main_ ? regular_ : 0, has_aux_a_, has_aux_b_, frames, data.isSolo());
    out.setBpm(data.getBpm());
    out.zero();
    std::array<float *, kMaxRegularChannels + 2> dst {};
    if (out.hasChannel(Channel::Main)) {
      for (int c = 0; c < regular_; c++) dst[static_cast<size_t>(c)] = out.getChannelData(c);
    }
    dst[static_cast<size_t>(regular_)] = out.getChannel(Channel::AuxA);
    dst[static_cast<size_t>(regular_ + 1)] = out.getChannel(Channel::AuxB);

    const float sr = static_cast<float>(sample_rate_);
    const float mix_step = 1.0f / (kFadeSeconds * sr);
    std::array<float, kMaxRegularChannels + 2> grain_frame {};
    for (int f = 0; f < frames; f++) {
      const int64_t slot = written_ % capacity_;
      for (int p = 0; p < planes; p++) {
	auto * src = in[static_cast<size_t>(p)];
	plane(p)[slot] = src ? src[f] : 0.0f;
      }
      written_++;
      if (!releasing_ && uniform() < spawn_chance_) spawn();
      mix_ = releasing_ ? std::max(0.0f, mix_ - mix_step) : std::min(1.0f, mix_ + mix_step);

      std::array<float, kMaxRegularChannels + 2> wet {};
      for (auto & g : grains_) {
	if (!g.active) continue;
	readGrain(g, grain_frame, planes);
	for (int p = 0; p < planes; p++) wet[static_cast<size_t>(p)] += grain_frame[static_cast<size_t>(p)];
	g.pos += 1.0;
	if (g.pos >= g.length) g.active = false;
      }
      for (int p = 0; p < planes; p++) {
	auto * d = dst[static_cast<size_t>(p)];
	if (!d) continue;
	auto * src = in[static_cast<size_t>(p)];
	float dry = src ? src[f] : 0.0f;
	d[f] = dry + (wet[static_cast<size_t>(p)] - dry) * mix_;
      }
    }
    if (releasing_ && mix_ <= 0.0f) active_ = false;
    data = std::move(out);
  }

private:
  struct Grain {
    bool active = false;
    double start = 0.0;  // where in the recording it begins, in frames since start()
    double pos = 0.0;    // frames played
    double length = 0.0;
    double rate = 1.0;
    float gain = 1.0f;
    std::array<float, 4> turn_cos {}, turn_sin {}; // the turn for each azimuthal order m (index m)
  };

  float uniform() { return 0.5f * (random_.next() + 1.0f); }
  float * plane(int p) { return ring_.data() + static_cast<size_t>(p) * static_cast<size_t>(capacity_); }

  void spawn() {
    auto slot = std::find_if(grains_.begin(), grains_.end(), [](const Grain & g) { return !g.active; });
    if (slot == grains_.end()) return;
    const float sr = static_cast<float>(sample_rate_);
    Grain g;
    g.length = static_cast<double>((kMinGrainSeconds + (kMaxGrainSeconds - kMinGrainSeconds) * uniform()) * sr);
    g.rate = std::pow(2.0, static_cast<double>((2.0f * uniform() - 1.0f) * kMaxDetuneCents) / 1200.0);
    // Far enough back that a grain played slightly fast never reads ahead of
    // what has been recorded.
    const double ahead = g.length * std::max(0.0, g.rate - 1.0) + 2.0;
    const double look_back = ahead + static_cast<double>(uniform() * kMaxLookBackSeconds * sr);
    g.start = static_cast<double>(written_) - look_back;
    if (g.start < 0.0) return; // not enough recorded yet
    g.gain = 0.5f + 0.5f * uniform();
    const float turn = (2.0f * uniform() - 1.0f) * kMaxTurnDegrees * 3.14159265f / 180.0f;
    for (int m = 1; m <= 3; m++) {
      g.turn_cos[static_cast<size_t>(m)] = cosf(static_cast<float>(m) * turn);
      g.turn_sin[static_cast<size_t>(m)] = sinf(static_cast<float>(m) * turn);
    }
    g.active = true;
    *slot = g;
  }

  // One frame of a grain, windowed and turned, into `frame`.
  void readGrain(const Grain & g, std::array<float, kMaxRegularChannels + 2> & frame, int planes) {
    const double t = g.start + g.pos * g.rate;
    const auto i0 = static_cast<int64_t>(t);
    const float frac = static_cast<float>(t - static_cast<double>(i0));
    const auto s0 = static_cast<size_t>(i0 % capacity_), s1 = static_cast<size_t>((i0 + 1) % capacity_);
    const float phase = static_cast<float>(g.pos / g.length);
    const float window = g.gain * 0.5f * (1.0f - cosf(2.0f * 3.14159265f * phase));
    for (int p = 0; p < planes; p++) {
      const float * r = plane(p);
      frame[static_cast<size_t>(p)] = (r[s0] + (r[s1] - r[s0]) * frac) * window;
    }
    // A turn about the vertical: each degree's cos(m az)/sin(m az) channel
    // pair rotates by m times the angle (ACN n = l*l + l + m).
    for (int l = 1; l <= order_; l++) {
      for (int m = 1; m <= l; m++) {
	auto & c = frame[static_cast<size_t>(l * l + l + m)];
	auto & s = frame[static_cast<size_t>(l * l + l - m)];
	const float cm = g.turn_cos[static_cast<size_t>(m)], sm = g.turn_sin[static_cast<size_t>(m)];
	const float nc = c * cm - s * sm, ns = c * sm + s * cm;
	c = nc;
	s = ns;
      }
    }
  }

  NoiseGenerator random_;
  std::vector<float> ring_; // regular_ + 2 planes of capacity_ frames
  std::array<Grain, kMaxGrains> grains_ {};
  int sample_rate_ = 44100, regular_ = 0, order_ = 0, capacity_ = 1;
  int64_t written_ = 0;
  float spawn_chance_ = 0.0f, mix_ = 0.0f;
  bool active_ = false, releasing_ = false;
  bool has_main_ = false, has_aux_a_ = false, has_aux_b_ = false;
};

#endif
