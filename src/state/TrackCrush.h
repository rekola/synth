#ifndef _TRACKCRUSH_H_
#define _TRACKCRUSH_H_

#include "../audio/AudioBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>

// Crush (PerformanceEffect::CRUSH): a track's output (or the mix) at a lower
// sample rate and fewer bits. Each sample is held for several frames, the
// same for every channel, so the sound stays where it is; the bit reduction
// is per channel and smears the image somewhat. Fades in and out.
class TrackCrush {
 public:
  static constexpr int kMaxChannels = 18; // third order plus both sends
  static constexpr float kFadeSeconds = 0.01f;

  void start(int sample_rate, float amount) {
    sample_rate_ = sample_rate;
    held_ = {};
    hold_phase_ = 1.0f; // take a fresh sample on the first frame
    mix_ = 0.0f;
    releasing_ = false;
    active_ = true;
    setAmount(amount);
  }

  // How crushed (0-1): from a quarter of the sample rate and 8 bits to a
  // 32nd and 4 bits.
  void setAmount(float amount) {
    releasing_ = false; // takes back a release still fading out
    const float a = std::clamp(amount, 0.0f, 1.0f);
    hold_step_ = 1.0f / (4.0f * std::pow(8.0f, a));
    levels_ = std::pow(2.0f, 7.0f - 4.0f * a); // half the steps of 8 to 4 bits, per sign
  }

  void release() { releasing_ = true; }
  bool isOff() const { return !active_; }

  void process(AudioBuffer & data) {
    if (!active_) return;
    const int frames = data.numberOfFrames();
    const int channels = std::min(static_cast<int>(data.numberOfChannels()), kMaxChannels);
    const float mix_step = 1.0f / (kFadeSeconds * static_cast<float>(sample_rate_));
    for (int f = 0; f < frames; f++) {
      hold_phase_ += hold_step_;
      const bool fresh = hold_phase_ >= 1.0f;
      if (fresh) hold_phase_ -= std::floor(hold_phase_);
      mix_ = releasing_ ? std::max(0.0f, mix_ - mix_step) : std::min(1.0f, mix_ + mix_step);
      for (int c = 0; c < channels; c++) {
        float & x = data.getChannelData(c)[f];
        auto & held = held_[static_cast<size_t>(c)];
        if (fresh) held = std::round(x * levels_) / levels_;
        x = mix_ >= 1.0f ? held : x + (held - x) * mix_;
      }
    }
    if (releasing_ && mix_ <= 0.0f) active_ = false;
  }

 private:
  std::array<float, kMaxChannels> held_{};
  int sample_rate_ = 44100;
  float hold_step_ = 0.25f, hold_phase_ = 0.0f, levels_ = 128.0f, mix_ = 0.0f;
  bool active_ = false, releasing_ = false;
};

#endif
