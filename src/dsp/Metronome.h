#ifndef DSP_METRONOME_H_
#define DSP_METRONOME_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsp {

// Click generator: a short decaying sine burst per beat, higher and louder
// on the downbeat. Clicks can start at any frame of a block and carry on
// into later ones.
class Metronome {
 public:
  explicit Metronome(int sample_rate) : sample_rate_(std::max(1, sample_rate)) { }

  void addClick(int start_frame, bool accent) {
    Click c;
    c.start = std::max(0, start_frame);
    c.accent = accent;
    clicks_.push_back(c);
  }

  // Adds the active clicks into `out` (frames samples).
  void render(float * out, int frames) {
    const float two_pi = 6.28318530718f;
    for (auto & c : clicks_) {
      float freq = c.accent ? kAccentHz : kBeatHz;
      float gain = c.accent ? kAccentGain : kBeatGain;
      float step = two_pi * freq / static_cast<float>(sample_rate_);
      float decay = std::exp(-1.0f / (kDecaySeconds * static_cast<float>(sample_rate_)));
      for (int i = c.start; i < frames && c.age < length(); i++, c.age++) {
        out[i] += gain * c.envelope * std::sin(c.phase);
        c.phase += step;
        c.envelope *= decay;
      }
      c.start = 0; // continues from frame 0 of the next block
    }
    clicks_.erase(std::remove_if(clicks_.begin(), clicks_.end(), [this](const Click & c) { return c.age >= length(); }), clicks_.end());
  }

  bool isActive() const { return !clicks_.empty(); }
  void clear() { clicks_.clear(); }

 private:
  struct Click {
    int start = 0;
    int age = 0;
    float phase = 0.0f;
    float envelope = 1.0f;
    bool accent = false;
  };

  int length() const { return static_cast<int>(kLengthSeconds * static_cast<float>(sample_rate_)); }

  static constexpr float kAccentHz = 1600.0f;
  static constexpr float kBeatHz = 1000.0f;
  static constexpr float kAccentGain = 0.5f;
  static constexpr float kBeatGain = 0.35f;
  static constexpr float kDecaySeconds = 0.015f;
  static constexpr float kLengthSeconds = 0.06f;

  int sample_rate_;
  std::vector<Click> clicks_;
};

} // namespace dsp

#endif
