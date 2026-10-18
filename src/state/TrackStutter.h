#ifndef _TRACKSTUTTER_H_
#define _TRACKSTUTTER_H_

#include "../audio/AudioBuffer.h"

#include <algorithm>
#include <array>
#include <vector>

// Stutter (PerformanceEffect::STUTTER): a track's own output, captured from
// a start point and then repeated as a loop, replacing the live signal until
// released. Every channel the track sends is captured - its ambisonic ones
// and both aux sends - so the repeat sits where the track does and feeds the
// send bus the same way. Short fades at the loop's seams and on the way in
// and out keep it from clicking.
class TrackStutter {
 public:
  static constexpr int kMaxRegularChannels = 16; // third order

  // Starts capturing `delay` frames from now, `slice` frames long, into room
  // for `capacity` (the longest slice a later setSlice() may ask for).
  void start(int delay, int slice, int capacity, int regular_channels, int fade) {
    regular_ = std::min(regular_channels, kMaxRegularChannels);
    capacity_ = std::max(1, capacity);
    size_t needed = static_cast<size_t>(regular_ + 2) * static_cast<size_t>(capacity_);
    if (buffer_.size() < needed) buffer_.resize(needed);
    slice_ = std::clamp(slice, 1, capacity_);
    fade_ = std::max(1, fade);
    wait_ = std::max(0, delay);
    captured_ = 0;
    pos_ = 0;
    loop_ = 0;
    release_pos_ = 0;
    has_main_ = has_aux_a_ = has_aux_b_ = false;
    phase_ = wait_ > 0 ? Phase::WAITING : Phase::CAPTURING;
  }

  // A new slice length: a shorter loop repeats the start of what was captured.
  void setSlice(int slice) {
    if (phase_ == Phase::OFF) return;
    slice_ = std::clamp(slice, 1, capacity_);
    if (phase_ == Phase::LOOPING || phase_ == Phase::RELEASING) {
      loop_ = std::min(slice_, captured_);
      pos_ %= loop_;
    }
  }

  // Back to the live signal: a loop fades into it, anything earlier simply
  // stops.
  void release() {
    if (phase_ == Phase::LOOPING) {
      phase_ = Phase::RELEASING;
      release_pos_ = 0;
    } else if (phase_ != Phase::RELEASING) {
      phase_ = Phase::OFF;
    }
  }

  bool isOff() const { return phase_ == Phase::OFF; }
  bool isLooping() const { return phase_ == Phase::LOOPING; }

  // Runs one block of the track's output through the stutter.
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

    // What the block carries: the live signal's channels, and the capture's
    // once the loop may sound.
    if (phase_ == Phase::WAITING || phase_ == Phase::CAPTURING) {
      has_main_ = has_main_ || in_main;
      has_aux_a_ = has_aux_a_ || in[static_cast<size_t>(regular_)];
      has_aux_b_ = has_aux_b_ || in[static_cast<size_t>(regular_ + 1)];
    }
    AudioBuffer out(has_main_ || in_main ? regular_ : 0, has_aux_a_ || in[static_cast<size_t>(regular_)], has_aux_b_ || in[static_cast<size_t>(regular_ + 1)], frames, data.isSolo());
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
      switch (phase_) {
        case Phase::OFF:
        case Phase::WAITING:
          copyLive(in, dst, planes, f, 1.0f);
          if (phase_ == Phase::WAITING && --wait_ <= 0) phase_ = Phase::CAPTURING;
          break;
        case Phase::CAPTURING: {
          for (int p = 0; p < planes; p++) {
            auto * src = in[static_cast<size_t>(p)];
            plane(p)[captured_] = src ? src[f] : 0.0f;
          }
          // The live signal fades out at the end of the capture, so the loop
          // can fade in from silence.
          float gain = std::min(1.0f, static_cast<float>(slice_ - captured_) / static_cast<float>(fade_));
          copyLive(in, dst, planes, f, gain);
          if (++captured_ >= slice_) {
            phase_ = Phase::LOOPING;
            loop_ = captured_;
            pos_ = 0;
          }
        } break;
        case Phase::LOOPING:
          copyLoop(dst, planes, f, 1.0f);
          advanceLoop();
          break;
        case Phase::RELEASING: {
          float t = static_cast<float>(release_pos_) / static_cast<float>(fade_);
          copyLoop(dst, planes, f, 1.0f - t);
          for (int p = 0; p < planes; p++) {
            auto * src = in[static_cast<size_t>(p)];
            auto * d = dst[static_cast<size_t>(p)];
            if (src && d) d[f] += src[f] * t;
          }
          advanceLoop();
          if (++release_pos_ >= fade_) phase_ = Phase::OFF;
        } break;
      }
    }
    data = std::move(out);
  }

 private:
  enum class Phase { OFF,
                     WAITING,
                     CAPTURING,
                     LOOPING,
                     RELEASING };

  float * plane(int p) { return buffer_.data() + static_cast<size_t>(p) * static_cast<size_t>(capacity_); }

  static void copyLive(const std::array<const float *, kMaxRegularChannels + 2> & in, const std::array<float *, kMaxRegularChannels + 2> & dst,
                       int planes, int f, float gain) {
    for (int p = 0; p < planes; p++) {
      auto * src = in[static_cast<size_t>(p)];
      auto * d = dst[static_cast<size_t>(p)];
      if (src && d) d[f] = src[f] * gain;
    }
  }

  // The loop at its current position, faded in and out at its seams.
  void copyLoop(const std::array<float *, kMaxRegularChannels + 2> & dst, int planes, int f, float gain) {
    int fade = std::max(1, std::min(fade_, loop_ / 2));
    float window = std::min({1.0f, static_cast<float>(pos_ + 1) / static_cast<float>(fade), static_cast<float>(loop_ - pos_) / static_cast<float>(fade)});
    for (int p = 0; p < planes; p++) {
      auto * d = dst[static_cast<size_t>(p)];
      if (d) d[f] = plane(p)[pos_] * window * gain;
    }
  }

  void advanceLoop() {
    if (++pos_ >= loop_) pos_ = 0;
  }

  Phase phase_ = Phase::OFF;
  std::vector<float> buffer_; // regular_ + 2 planes of capacity_ frames
  int regular_ = 0, capacity_ = 1;
  int slice_ = 1, fade_ = 1;
  int wait_ = 0, captured_ = 0, loop_ = 0, pos_ = 0, release_pos_ = 0;
  bool has_main_ = false, has_aux_a_ = false, has_aux_b_ = false;
};

#endif
