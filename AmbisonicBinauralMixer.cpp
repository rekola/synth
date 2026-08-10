#include "AmbisonicBinauralMixer.h"
#include "SofaFileResolver.h"

#include <mysofa.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <fmt/core.h>

using namespace std;

namespace {

// Three speaker layouts, chosen by ambisonic order (see
// speakerDirectionsFor): order 1 (4 channels, W/Y/Z/X only) keeps the
// original 8-speaker cube - a 12-speaker icosahedron wouldn't buy anything
// decoding from just 4 basis functions, and an evenly-spaced cube is
// already a good fit for FOA content. Order 2 (9 channels) moves to a
// 12-speaker icosahedron, which is what actually exploits the 5 additional
// degree-2 basis functions for finer spatial resolution. Order 3 (16
// channels) moves to a 26-point Lebedev grid - the icosahedron is only a
// spherical 5-design, sufficient for order 2's t>=5 requirement but not
// order 3's t>=2*3+1=7 (see AmbisonicEncoding.h's lebedev26Directions()).
// Azimuth here is in this engine's convention (positive = right, see
// PanLaw.h) - converted to libmysofa/SOFA's convention (positive = left,
// measured from the +x/front axis) via a sign flip below, in the loop that
// builds each SpeakerFilter.

// Cube vertices (order 1): the same 8 directions the shared send bus's
// spatial reverb spreads its taps over - see AmbisonicEncoding.h's
// cubeVertexDirections(), the single shared source of truth for these
// constants.

// Icosahedron vertices: 2 poles + two 5-vertex rings at the true
// icosahedron-vertex elevation (atan(0.5), the angle between a
// pentagonal-ring vertex and the polar axis), the rings offset 36 degrees
// from each other.
constexpr float kIcosahedronElevation = 26.56505117707799f; // atan(0.5), degrees

std::vector<AmbisonicDirection> speakerDirectionsFor(int ambisonic_channels) {
  if (ambisonic_channels <= 4) {
    auto cube = cubeVertexDirections();
    return std::vector<AmbisonicDirection>(cube.begin(), cube.end());
  }
  if (ambisonic_channels <= 9) {
    return {
      { 0.0f, 90.0f }, { 0.0f, -90.0f },
      { 0.0f, kIcosahedronElevation }, { 72.0f, kIcosahedronElevation }, { 144.0f, kIcosahedronElevation }, { 216.0f, kIcosahedronElevation }, { 288.0f, kIcosahedronElevation },
      { 36.0f, -kIcosahedronElevation }, { 108.0f, -kIcosahedronElevation }, { 180.0f, -kIcosahedronElevation }, { 252.0f, -kIcosahedronElevation }, { 324.0f, -kIcosahedronElevation },
    };
  }
  return lebedev26Directions();
}

// gain_trim_'s single tunable: target output level for the worst-case
// (diffuse/unpositioned, W-only) source, decoded through this instance's
// own speaker count, max-rE weighting, and the actual measured filter
// energy of whichever HRTF dataset is loaded - see gain_trim_'s own
// derivation (AmbisonicBinauralMixer.h) for the full formula and exactly
// what "filter energy" means (deliberately not plain RMS - see that
// comment for why). "How loud the worst case should be" is a product
// choice, not something purely derivable from the HRIR data itself, so
// this is empirically calibrated rather than derived: chosen so real
// program material renders at the same peak/RMS ballpark (no clipping,
// comparable loudness across all three orders) this class already
// produced before per-dataset filter-energy normalization existed at all -
// confirmed by rendering the same fixture song at each order and comparing
// peak/RMS/clip-count, not just picked by inspection. Listen and adjust
// this one constant if levels still seem off on a future dataset swap -
// there is deliberately only one tunable here, not a pair of
// independently-arbitrary constants multiplied together.
constexpr float kGainTrimTarget = 0.5f;

} // namespace

AmbisonicBinauralMixer::AmbisonicBinauralMixer(int ambisonic_channels, int outSampleRate)
  : Mixer(2, outSampleRate), ambisonic_channels_(ambisonic_channels), buffer_(static_cast<short>(ambisonic_channels), 0),
    gain_trim_(kGainTrimTarget) { // placeholder - overwritten below once actually ready, see its own doc comment
  auto sofa_path = findDefaultSofaFile();
  if (sofa_path.empty()) return;

  int filterlength = 0, err = 0;
  easy_ = mysofa_open(sofa_path.c_str(), static_cast<float>(outSampleRate), &filterlength, &err);
  if (!easy_ || err != 0 || filterlength <= 0) {
    easy_ = nullptr;
    return;
  }

  fmt::print(stderr, "Using SOFA file {} for binaural decoding\n", sofa_path);

  // max-rE decode weighting (see AmbisonicEncoding.h's maxReGainsPerDegree)
  // - computed once here (order-derived from ambisonic_channels_ via
  // acnDegree of the highest channel index, since ambisonic_channels_ is
  // always exactly (order+1)^2), not per speaker or per sample: every
  // speaker's row gets the same per-degree weight k*g_l, only their
  // per-direction encode gains differ. k is the separate energy-
  // renormalization scalar (mean-square gain across channels preserved
  // relative to the unweighted decode) - see AmbisonicEncoding.h's own
  // comment on maxReGainsPerDegree for why it's not folded into g_l itself.
  int order = acnDegree(ambisonic_channels_ - 1);
  vector<float> max_re_gains(static_cast<size_t>(order) + 1);
  maxReGainsPerDegree(order, max_re_gains.data());
  float renorm_numerator = 0.0f, renorm_denominator = 0.0f;
  for (int l = 0; l <= order; l++) {
    float count = 2.0f * static_cast<float>(l) + 1.0f;
    renorm_numerator += count;
    renorm_denominator += count * max_re_gains[static_cast<size_t>(l)] * max_re_gains[static_cast<size_t>(l)];
  }
  float renorm_k = sqrtf(renorm_numerator / renorm_denominator);

  auto speaker_directions = speakerDirectionsFor(ambisonic_channels_);

  int max_ir_and_delay = 0;
  double ir_sum_sq = 0.0;
  size_t ir_filter_count = 0; // one per (speaker, ear), NOT per sample - see gain_trim_'s derivation below
  for (auto & dir : speaker_directions) {
    SpeakerFilter filter;
    filter.decode_gains = computeAmbisonicGains(SphericalPosition{ dir.azimuth, dir.elevation, 1.0f });
    for (int c = 0; c < ambisonic_channels_; c++) {
      filter.decode_gains[static_cast<size_t>(c)] *= renorm_k * max_re_gains[static_cast<size_t>(acnDegree(c))];
    }

    constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
    float sofa_azimuth = -dir.azimuth * kDeg2Rad; // this engine: +az = right; SOFA: +az = left
    float el = dir.elevation * kDeg2Rad;
    float x = cosf(el) * cosf(sofa_azimuth);
    float y = cosf(el) * sinf(sofa_azimuth);
    float z = sinf(el);

    filter.left_ir.resize(static_cast<size_t>(filterlength));
    filter.right_ir.resize(static_cast<size_t>(filterlength));
    float delay_left = 0.0f, delay_right = 0.0f;
    mysofa_getfilter_float(easy_, x, y, z, filter.left_ir.data(), filter.right_ir.data(), &delay_left, &delay_right);
    filter.left_delay = static_cast<int>(lroundf(delay_left));
    filter.right_delay = static_cast<int>(lroundf(delay_right));
    if (filter.left_delay < 0) filter.left_delay = 0;
    if (filter.right_delay < 0) filter.right_delay = 0;

    int candidate = filterlength + std::max(filter.left_delay, filter.right_delay);
    if (candidate > max_ir_and_delay) max_ir_and_delay = candidate;

    // Accumulated for gain_trim_'s dataset-loudness correction below - see
    // that computation's own comment for why this can't be decided from
    // speaker count/max-rE weight alone.
    for (float v : filter.left_ir) ir_sum_sq += static_cast<double>(v) * v;
    for (float v : filter.right_ir) ir_sum_sq += static_cast<double>(v) * v;
    ir_filter_count += 2; // left_ir + right_ir

    speakers_.push_back(std::move(filter));
  }

  left_tail_.assign(static_cast<size_t>(max_ir_and_delay), 0.0f);
  right_tail_.assign(static_cast<size_t>(max_ir_and_delay), 0.0f);

  // gain_trim_ - see the member's own doc comment (AmbisonicBinauralMixer.h)
  // for the full derivation and worked examples per order. dataset_filter_energy
  // is measured fresh from whatever's actually loaded (accumulated above
  // while fetching every speaker's HRIR): the L2 norm ("energy") of a
  // typical filter, sqrt(mean over every (speaker,ear) filter of the sum
  // of its own squared taps) - NOT a plain per-tap RMS. This distinction
  // matters because convolution's output power scales with a filter's
  // total energy (RMS * sqrt(filter length)), not its RMS alone; datasets
  // can differ in both dimensions independently (confirmed: the KU100
  // compilation now in use has both a higher per-tap RMS AND a much
  // shorter filter length than the previously-installed set - normalizing
  // by RMS alone got the direction right but the magnitude badly wrong,
  // undercorrecting for the length difference). This is what makes
  // gain_trim_ correct regardless of which SOFA file loads, unlike the old
  // formula (speaker count and max-rE weight only), which had no
  // dependence on the HRIR data at all and clipped once a genuinely
  // louder/differently-shaped dataset was loaded.
  float dataset_filter_energy = ir_filter_count > 0 ? sqrtf(static_cast<float>(ir_sum_sq / static_cast<double>(ir_filter_count))) : kGainTrimTarget;
  gain_trim_ = kGainTrimTarget / (static_cast<float>(speaker_directions.size()) * renorm_k * max_re_gains[0] * dataset_filter_energy);

  ready_ = true;
}

AmbisonicBinauralMixer::~AmbisonicBinauralMixer() {
  if (easy_) mysofa_close(easy_);
}

void
AmbisonicBinauralMixer::reset() {
  buffer_.zero();
}

void
AmbisonicBinauralMixer::accumulate(const AudioBuffer & input) {
  if (buffer_.numberOfFrames() != input.numberOfFrames()) {
    buffer_ = AudioBuffer(static_cast<short>(ambisonic_channels_), input.numberOfFrames());
    buffer_.zero();
  }
  // mixNamed() rather than mix() - `input` (a track's rendered output) may
  // carry AuxA/AuxB trailing its regular ambisonic channels (see
  // Mixer.h); buffer_ never marks them present (raw-count constructor) so
  // they're silently ignored here.
  buffer_.mixNamed(input);
}

AudioBuffer
AmbisonicBinauralMixer::encode() {
  int frames = buffer_.numberOfFrames();
  size_t tail_len = left_tail_.size();
  size_t acc_size = static_cast<size_t>(frames) + tail_len;

  // Reused across calls (see the members' own comments) - resize() only
  // reallocates the first time a given block size grows past whatever was
  // previously reserved; once warmed up (block size is constant in
  // practice), this is allocation-free.
  left_acc_.resize(acc_size);
  right_acc_.resize(acc_size);
  speaker_signal_.resize(static_cast<size_t>(frames));

  for (size_t i = 0; i < tail_len; i++) {
    left_acc_[i] = left_tail_[i];
    right_acc_[i] = right_tail_[i];
  }
  std::fill(left_acc_.begin() + static_cast<long>(tail_len), left_acc_.end(), 0.0f);
  std::fill(right_acc_.begin() + static_cast<long>(tail_len), right_acc_.end(), 0.0f);

  // buffer_ is constructed via the raw-count constructor, so it never
  // marks any channel present - it always holds exactly ambisonic_channels_
  // regular channels (never any sends), gathered once per call. Clamped to
  // kAmbisonicChannelCount (mirroring AmbisonicVoiceEncoder::encodeBlock's
  // own n = std::min(...) above) rather than trusting regular outright -
  // today this can never actually exceed the array (ChannelConfiguration
  // caps order at kAmbisonicOrder), but that's an invariant enforced far
  // away from this fixed-size stack array, not something this function can
  // see for itself.
  int regular = std::min(static_cast<int>(buffer_.numberOfChannels()), kAmbisonicChannelCount);
  float * channels[kAmbisonicChannelCount] = {};
  for (int c = 0; c < regular; c++) channels[c] = buffer_.getChannelData(c);

  for (auto & speaker : speakers_) {
    auto & g = speaker.decode_gains;
    for (int i = 0; i < frames; i++) {
      float s = 0.0f;
      for (int c = 0; c < regular; c++) s += g[static_cast<size_t>(c)] * channels[c][i];
      speaker_signal_[static_cast<size_t>(i)] = s;
    }

    auto ir_len = speaker.left_ir.size();
    for (int i = 0; i < frames; i++) {
      float v = speaker_signal_[static_cast<size_t>(i)];
      if (v == 0.0f) continue;
      size_t left_base = static_cast<size_t>(i + speaker.left_delay);
      size_t right_base = static_cast<size_t>(i + speaker.right_delay);
      for (size_t k = 0; k < ir_len; k++) {
	left_acc_[left_base + k] += v * speaker.left_ir[k];
	right_acc_[right_base + k] += v * speaker.right_ir[k];
      }
    }
  }

  AudioBuffer out(getOutChannels(), frames);
  auto out_left = out.getChannelData(0), out_right = out.getChannelData(1);
  for (int i = 0; i < frames; i++) {
    float l = left_acc_[static_cast<size_t>(i)] * gain_trim_;
    float r = right_acc_[static_cast<size_t>(i)] * gain_trim_;
    if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
    if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
    out_left[i] = l;
    out_right[i] = r;
  }

  for (size_t i = 0; i < tail_len; i++) {
    left_tail_[i] = left_acc_[static_cast<size_t>(frames) + i];
    right_tail_[i] = right_acc_[static_cast<size_t>(frames) + i];
  }

  return out;
}
