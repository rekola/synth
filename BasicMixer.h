#ifndef _BASICMIXER_H_
#define _BASICMIXER_H_

#include "Mixer.h"

#include <cmath>

class BasicMixer : public Mixer {
 public:
  BasicMixer(short _out_channels, int _outSampleRate) : Mixer(_out_channels, _outSampleRate) { }
  
  void reset() override {
    clear();
  }
  
  void accumulate(const SampleData & input, float volume) override {
    if (!buffer || input.size() != frames) {
      frames = input.size();
      buffer = std::unique_ptr<float[]>(new float[frames * getOutChannels()]);
      clear();
    }

    if (getOutChannels() == input.numberOfChannels()) {
      for (size_t i = 0; i < input.numberOfChannels() * input.size(); i++) {
	buffer[i] += input.data()[i];
      }
    } else if (getOutChannels() == 2 && input.numberOfChannels() == 1) {
      for (size_t i = 0; i < input.size(); i++) {
	float ss = input.data()[i];
	
	buffer[2 * i + 0] += ss;
	buffer[2 * i + 1] += ss;
      }
    } else {
      assert(0);
    }
  }
  
  SampleData encode(float master_volume) override {
    SampleData output(getOutChannels(), frames);
    for (size_t i = 0; i < getOutChannels() * frames; i++) {
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
