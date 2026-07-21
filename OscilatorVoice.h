#ifndef _OSCILATORVOICE_H_
#define _OSCILATORVOICE_H_

#include "InstrumentVoice.h"
#include "WaveformType.h"
#include "PanLaw.h"
#include "SphericalPosition.h"

class OscilatorVoice : public InstrumentVoice {
public:
  OscilatorVoice(ChannelConfiguration config, const SphericalPosition & position, float detune, float start_phase, WaveformType type, float level, float pulse_width, float send_a = 0.0f, float send_b = 0.0f)
    : InstrumentVoice(config, position, detune, start_phase, send_a, send_b), type_(type), level_(level), pulse_width_(pulse_width) {
  }

  SampleData render(int frames) override {
    // base_gain (undistanced) feeds the sends - see InstrumentVoice::getDistanceGain().
    float base_gain = decibelsToGain(getGainDB()) * level_;
    float gain = base_gain * getDistanceGain();

    auto channels = regularChannelsFor(getChannelConfiguration());
    if (getSendA() > 0.0f) channels.push_back(Channel::SendA);
    if (getSendB() > 0.0f) channels.push_back(Channel::SendB);

    SampleData data(channels, frames);
    auto num_channels = getChannelConfiguration().numberOfChannels();
    auto left_buffer = data.getChannelData(0);
    auto right_buffer = num_channels == 2 ? data.getChannelData(1) : nullptr;
    auto send_a = data.getChannel(Channel::SendA);
    auto send_b = data.getChannel(Channel::SendB);

    double pos = getSourceSamplePosition() / getChannelConfiguration().getAudioOutSampleRate();
    double rate = (double)getFrequency() / getChannelConfiguration().getAudioOutSampleRate();

    float left_gain = gain, right_gain = gain;
    if (right_buffer) {
      auto gains = panToStereoGains(azimuthToPan(getAzimuth()));
      left_gain = gains.left * gain;
      right_gain = gains.right * gain;
    }

    SampleData modulator;
    const float * modulator_data = nullptr;
    if (!getChildren().empty()) {
      modulator = InstrumentVoice::render(frames);
      modulator_data = modulator.getChannelData(0);
    }

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

      left_buffer[k] = left_gain * a;
      if (right_buffer) right_buffer[k] = right_gain * a;
      if (send_a) send_a[k] = base_gain * a * getSendA();
      if (send_b) send_b[k] = base_gain * a * getSendB();

      pos += rate;
    }

    stepForward(frames);

    data.setNonZero();
    return data;
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
};


#endif
