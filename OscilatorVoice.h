#ifndef _OSCILATORVOICE_H_
#define _OSCILATORVOICE_H_

#include "InstrumentVoice.h"
#include "WaveformType.h"

class OscilatorVoice : public InstrumentVoice {
public:
  OscilatorVoice(ChannelConfiguration config, float azimuth, float detune, float start_phase, WaveformType type, float level, float pulse_width)
    : InstrumentVoice(config, azimuth, detune, start_phase), type_(type), level_(level), pulse_width_(pulse_width) {
  }

  SampleData render(int frames) override {    
    float gain = decibelsToGain(getGainDB()) * level_;

    SampleData data(getChannelConfiguration(), frames);
    auto num_channels = data.numberOfChannels();
    auto left_buffer = data.getChannelData(0);

    double pos = getSourceSamplePosition() / getChannelConfiguration().getAudioOutSampleRate();
    double rate = (double)getFrequency() / getChannelConfiguration().getAudioOutSampleRate();

    if (num_channels == 2) {
      auto right_buffer = data.getChannelData(1);
      float pan = sin(getAzimuth() / 180.0f * M_PI) / 2;
      if (pan < -0.5) pan = -0.5;
      else if (pan > 0.5) pan = 0.5;
      float left_gain = sqrtf(0.5f - pan) * gain, right_gain = sqrtf(0.5f + pan) * gain;
      
      if (!getChildren().empty()) {
	// render children
	auto modulator = InstrumentVoice::render(frames);
	auto modulator_data = modulator.getChannelData(0);
	
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_sine(pos + modulator_data[k]);
	    
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;

	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_saw(pos + modulator_data[k]);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_triangle(pos + modulator_data[k]);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_square(pos + modulator_data[k]);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	}
      } else {
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_sine(pos);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_saw(pos);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_triangle(pos);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    auto a = create_square(pos);
	    left_buffer[k] = left_gain * a;
	    right_buffer[k] = right_gain * a;
	    pos += rate;
	  }
	  break;
	}
      }
    } else {
      if (!getChildren().empty()) {
	auto modulator = InstrumentVoice::render(frames);
	auto modulator_data = modulator.getChannelData(0);
	
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {	  
	    left_buffer[k] = create_sine(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_saw(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_triangle(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_square(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	}
      } else {
	switch (type_) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_sine(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_saw(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_triangle(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    left_buffer[k] = create_square(pos) * gain;
	    pos += rate;
	  }
	  break;
	}
      }
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
