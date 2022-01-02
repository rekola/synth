#ifndef _BASICMIXER_H_
#define _BASICMIXER_H_

#include "Mixer.h"

#include <cmath>

class BasicMixer : public Mixer {
 public:
  BasicMixer(unsigned int _outSampleRate) : Mixer(_outSampleRate) { }
  
  void reset() override {
    clear();
  }
  
  void accumulate(const SampleData & input, float volume, float distance, float azimuth, float elevation) override {
    float pan = 0.5 + 0.5 * sin(azimuth / 180.0 * M_PI);

    if (!buffer || input.size() != frames) {
      frames = input.size();
      buffer = std::unique_ptr<float[]>(new float[frames * 2]);
      clear();
    }
    
    if (input.getChannels() == 1) {
      for (size_t i = 0; i < input.size(); i++) {
	float ss = input.data()[i];
	
	buffer[2 * i + 0] += ss * sqrtf(1.0 - pan);
	buffer[2 * i + 1] += ss * sqrtf(pan);
      }
    } else {
      float left_f = cos(pan * M_PI / 2), right_f = sin(pan * M_PI / 2);
      for (size_t i = 0; i < input.size(); i++) {
	float left = input.data()[2 * i + 0], right = input.data()[2 * i + 1];

	buffer[2 * i + 0] += left_f * left;
	buffer[2 * i + 1] += right_f * right; 
      }
    }
  }
  
  SampleData encode(float master_volume) override {
    SampleData output(2, frames);
    for (size_t i = 0; i < 2 * output.size(); i++) {
      float s = buffer[i] * master_volume;      
      if (s > 1.0) s = 1.0;
      else if (s < -1.0) s = -1.0;
      output.data()[i] = s;
    }
    return output;
  }

protected:
  void clear() {
    if (buffer && frames) {
      memset(buffer.get(), 0, frames * 2 * sizeof(float));
    }
  }

private:
  std::unique_ptr<float[]> buffer;
  size_t frames = 0;
};

#endif
