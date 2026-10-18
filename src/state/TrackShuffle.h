#ifndef _TRACKSHUFFLE_H_
#define _TRACKSHUFFLE_H_

#include "../audio/AudioBuffer.h"
#include "../dsp/NoiseGenerator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// Shuffle (PerformanceEffect::SHUFFLE): a track's output (or the mix),
// recorded from a start point for up to a bar and played back as grid
// slices in a random order while held. Each slice is any one already
// recorded, the one being recorded included, so it starts as the live sound
// and scatters from there; once the bar is full it keeps reshuffling it.
// Every channel is recorded, so the slices stay where the source sits. A
// slice that doesn't continue the one before crossfades from it.
class TrackShuffle {
 public:
  static constexpr int kMaxRegularChannels = 16; // third order

  explicit TrackShuffle(uint32_t seed = 1) : random_(seed) {}

  // Starts recording `delay` frames from now, into room for `capacity`
  // frames, playing slices `slice` frames long.
  void start(int delay, int slice, int capacity, int regular_channels, int fade) {
    regular_ = std::min(regular_channels, kMaxRegularChannels);
    capacity_ = std::max(1, capacity);
    size_t needed = static_cast<size_t>(regular_ + 2) * static_cast<size_t>(capacity_);
    if (buffer_.size() < needed) buffer_.resize(needed);
    slice_ = std::clamp(slice, 1, capacity_);
    fade_ = std::max(1, fade);
    wait_ = std::max(0, delay);
    captured_ = 0;
    cur_start_ = 0;
    cur_len_ = 0;
    cur_pos_ = 0;
    tail_ = -1;
    release_pos_ = 0;
    has_main_ = has_aux_a_ = has_aux_b_ = false;
    phase_ = wait_ > 0 ? Phase::WAITING : Phase::PLAYING;
  }

  // The slice length from the next slice on.
  void setSlice(int slice) { slice_ = std::clamp(slice, 1, capacity_); }

  // Back to the live signal, crossfading into it.
  void release() {
    if (phase_ == Phase::PLAYING) {
      phase_ = Phase::RELEASING;
      release_pos_ = 0;
    } else if (phase_ == Phase::WAITING) {
      phase_ = Phase::OFF;
    }
  }

  bool isOff() const { return phase_ == Phase::OFF; }
  // Where the slice playing now starts in the recording, in frames.
  int currentSliceStart() const { return cur_start_; }

  void process(AudioBuffer & data) {
    if (phase_ == Phase::OFF) return;
    const int frames = data.numberOfFrames();
    const bool in_main = data.hasChannel(Channel::Main) && data.regularChannelCount() == regular_;
    std::array<const float *, kMaxRegularChannels + 2> in{};
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
    std::array<float *, kMaxRegularChannels + 2> dst{};
    if (out.hasChannel(Channel::Main)) {
      for (int c = 0; c < regular_; c++) dst[static_cast<size_t>(c)] = out.getChannelData(c);
    }
    dst[static_cast<size_t>(regular_)] = out.getChannel(Channel::AuxA);
    dst[static_cast<size_t>(regular_ + 1)] = out.getChannel(Channel::AuxB);

    const int planes = regular_ + 2;
    for (int f = 0; f < frames; f++) {
      if (phase_ == Phase::OFF || phase_ == Phase::WAITING) {
        for (int p = 0; p < planes; p++) {
          auto * src = in[static_cast<size_t>(p)];
          auto * d = dst[static_cast<size_t>(p)];
          if (src && d) d[f] = src[f];
        }
        if (phase_ == Phase::WAITING && --wait_ <= 0) phase_ = Phase::PLAYING;
        continue;
      }
      if (captured_ < capacity_) {
        for (int p = 0; p < planes; p++) {
          auto * src = in[static_cast<size_t>(p)];
          plane(p)[captured_] = src ? src[f] : 0.0f;
        }
        captured_++;
      }
      if (cur_pos_ >= cur_len_) nextSlice();
      // Frames written before this one; the live slice reads the one just
      // written.
      const int readable = captured_;
      const int at = cur_start_ + cur_pos_;
      float gain = 1.0f, tail_gain = 0.0f;
      int tail_at = -1;
      if (tail_ >= 0 && cur_pos_ < fade_) {
        gain = static_cast<float>(cur_pos_ + 1) / static_cast<float>(fade_);
        tail_gain = 1.0f - gain;
        tail_at = tail_ + cur_pos_;
        if (tail_at >= readable) tail_at = -1;
      }
      float live_gain = 0.0f;
      if (phase_ == Phase::RELEASING) {
        live_gain = static_cast<float>(release_pos_) / static_cast<float>(fade_);
        gain *= 1.0f - live_gain;
        tail_gain *= 1.0f - live_gain;
      }
      for (int p = 0; p < planes; p++) {
        auto * d = dst[static_cast<size_t>(p)];
        if (!d) continue;
        const float * r = plane(p);
        float v = at < readable ? r[at] * gain : 0.0f;
        if (tail_at >= 0) v += r[tail_at] * tail_gain;
        if (auto * src = in[static_cast<size_t>(p)]) v += src[f] * live_gain;
        d[f] = v;
      }
      cur_pos_++;
      if (phase_ == Phase::RELEASING && ++release_pos_ >= fade_) phase_ = Phase::OFF;
    }
    data = std::move(out);
  }

 private:
  enum class Phase { OFF,
                     WAITING,
                     PLAYING,
                     RELEASING };

  float uniform() { return 0.5f * (random_.next() + 1.0f); }
  float * plane(int p) { return buffer_.data() + static_cast<size_t>(p) * static_cast<size_t>(capacity_); }

  // Picks the next slice: any start on the slice grid that has begun
  // recording (the live position included) and fits in the recording.
  void nextSlice() {
    const int prev_end = cur_start_ + cur_len_;
    const int len = slice_;
    // captured_ includes this frame, so the live slice starts one back.
    const int live = captured_ - 1;
    const int last = std::min(live, capacity_ - len) / len;
    int start = 0;
    if (cur_len_ == 0) {
      start = std::max(0, live) / len * len; // the first slice is the live one
      if (start > capacity_ - len) start = 0;
    } else if (last > 0) {
      start = std::min(static_cast<int>(uniform() * static_cast<float>(last + 1)), last) * len;
    }
    tail_ = cur_len_ > 0 && start != prev_end ? prev_end : -1;
    cur_start_ = start;
    cur_len_ = len;
    cur_pos_ = 0;
  }

  Phase phase_ = Phase::OFF;
  NoiseGenerator random_;
  std::vector<float> buffer_; // regular_ + 2 planes of capacity_ frames
  int regular_ = 0, capacity_ = 1;
  int slice_ = 1, fade_ = 1, wait_ = 0;
  int captured_ = 0;
  int cur_start_ = 0, cur_len_ = 0, cur_pos_ = 0;
  int tail_ = -1; // where the previous slice ended, while crossfading from it
  int release_pos_ = 0;
  bool has_main_ = false, has_aux_a_ = false, has_aux_b_ = false;
};

#endif
