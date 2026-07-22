#ifndef _OSCILATORVOICE_H_
#define _OSCILATORVOICE_H_

#include "InstrumentVoice.h"
#include "WaveformType.h"
#include "SphericalPosition.h"

#include <vector>

class OscilatorVoice : public InstrumentVoice {
public:
  OscilatorVoice(ChannelConfiguration config, const SphericalPosition & position, float detune, float start_phase, WaveformType type, float level, float pulse_width, float send_a = 0.0f, float send_b = 0.0f)
    : InstrumentVoice(config, position, detune, start_phase, send_a, send_b), type_(type), level_(level), pulse_width_(pulse_width) {
  }

  SampleData render(int frames) override {
    float gain = decibelsToGain(getGainDB()) * level_ * getDistanceGain();

    double pos = getSourceSamplePosition() / getChannelConfiguration().getAudioOutSampleRate();
    double rate = (double)getFrequency() / getChannelConfiguration().getAudioOutSampleRate();

    SampleData modulator;
    const float * modulator_data = nullptr;
    if (!getChildren().empty()) {
      modulator = InstrumentVoice::render(frames);
      modulator_data = modulator.getChannelData(0);
    }

    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));

    for (int k = 0; k < frames; k++) {
      double phase = pos + (modulator_data ? modulator_data[k] : 0.0);
      float a;
      switch (type_) {
      case WaveformType::SINE: a = create_sine(phase); break;
      case WaveformType::SAW: a = create_saw(phase); break;
      case WaveformType::TRIANGLE: a = create_triangle(phase); break;
      case WaveformType::SQUARE: a = create_square(phase); break;
      default: a = 0.0f; break;
      }

      dry_[static_cast<size_t>(k)] = gain * a;

      pos += rate;
    }

    stepForward(frames);

    return encodePosition(dry_.data(), frames);
  }

private:
  static constexpr float pi = float(M_PI);
  
  static inline float create_sine(double i0) {
    float i = i0 - (long long)(i0);
    return sinf(2.0f * pi * i);
  }
  
  static inline float create_saw(double i0) {
    float i = i0 - (long long)(i0);
    return i < 0.5f ? 2.0f * i : 2.0f * i - 2.0f;
  }
  
  static inline float create_triangle(double i0) {
    float i = i0 - (long long)(i0);
    return i < 0.5f ? 1.0f - 4.0f * i : 4.0f * i - 3;
  }
  
  inline float create_square(double i0) const {
    float i = i0 - (long long)(i0);
    return i < pulse_width_ ? -1.0f : 1.0f;
  }
  

  WaveformType type_;
  float level_;
  float pulse_width_;
  std::vector<float> dry_;
};


#endif
