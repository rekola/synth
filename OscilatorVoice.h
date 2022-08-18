#ifndef _OSCILATORVOICE_H_
#define _OSCILATORVOICE_H_

#include "InstrumentVoice.h"
#include "WaveformType.h"

class OscilatorVoice : public InstrumentVoice {
public:
  OscilatorVoice(ChannelConfiguration config, float azimuth, float start_phase, WaveformType type, float level)
    : InstrumentVoice(config, azimuth, start_phase), type_(type), level_(level) {
  }

  SampleData render(int frames) override {    
    float gain = decibelsToGain(getGainDB()) * level_;

    SampleData data(getChannelConfiguration(), frames);
    auto num_channels = data.numberOfChannels();
    auto buffer = data.data();
    
    double pos = getSourceSamplePosition() / getChannelConfiguration().getAudioOutSampleRate();
    double rate = (double)getFrequency() / getChannelConfiguration().getAudioOutSampleRate();

    if (num_channels == 2) {
      float pan = sin(getAzimuth() / 180.0f * M_PI) / 2;
      if (pan < -0.5) pan = -0.5;
      else if (pan > 0.5) pan = 0.5;
      float left_gain = sqrtf(0.5f - pan) * gain, right_gain = sin(0.5f + pan) * gain;
      
      if (!getChildren().empty() && type_ != WaveformType::NOISE) {
	// render children
	auto modulator = InstrumentVoice::render(frames);
	assert(modulator.numberOfChannels() == 2);
	auto modulator_data = modulator.data();
	
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_sine(pos + modulator_data[2 * k + 0]);
	    
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;

	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_saw(pos + modulator_data[2 * k + 0]);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_triangle(pos + modulator_data[2 * k + 0]);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_square(pos + modulator_data[2 * k + 0]);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  // noise cannot be modulated
	  break;
	}
      } else {
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_sine(pos);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_saw(pos);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_triangle(pos);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_square(pos);
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_noise();
	    buffer[2 * k + 0] = left_gain * a;
	    buffer[2 * k + 1] = right_gain * a;
	    pos += rate;
	  }
	  break;
	}
      }
    } else {
      if (!getChildren().empty() && type_ != WaveformType::NOISE) {
	auto modulator = InstrumentVoice::render(frames);
	auto modulator_data = modulator.data();
	
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {	  
	    buffer[k] = create_sine(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_saw(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_triangle(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_square(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  // noise cannot be modulated
	  break;
	}
      } else {
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_sine(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_saw(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_triangle(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_square(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_noise() * gain;
	    pos += rate;
	  }
	  break;
	}
      }
    }
    
    stepForward(frames);

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
  
  static inline float create_square(double i0) {
    float i = i0 - (long long)(i0);
    return i < 0.5f ? -1.0f : 1.0f;
  }
  
  static inline float create_noise() {
    return getRandF() * 2.0f - 1.0f;
  }

  WaveformType type_;
  float level_;
};


#endif
