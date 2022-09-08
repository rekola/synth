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
  
  void accumulate(const SampleData & input) override {
    if (!buffer || input.size() != frames) {
      frames = input.size();
      buffer = std::unique_ptr<float[]>(new float[frames * getOutChannels()]);
      clear();
    }

    if (getOutChannels() == input.numberOfChannels()) {
      for (int j = 0; j < input.numberOfChannels(); j++) {
	auto input_buffer = input.getChannelData(j);
	for (int i = 0; i < input.size(); i++) {
	  buffer[j * frames + i] += input_buffer[i];
	}
      }
    } else if (getOutChannels() == 2 && input.numberOfChannels() == 1) {
      for (int j = 0; j < input.numberOfChannels(); j++) {
	auto input_buffer = input.getChannelData(0);
	for (int i = 0; i < input.size(); i++) {
	  buffer[j * frames + i] += input_buffer[i];
	}
      }
    } else {
      assert(0);
    }
  }
  
  SampleData encode() override {
    SampleData output(getOutChannels(), frames);
    for (int j = 0; j < output.numberOfChannels(); j++) {
      auto output_buffer = output.getChannelData(j);
      for (int i = 0; i < output.size(); i++) {
	float s = buffer[j * frames + i];
	if (s > 1.0) s = 1.0;
	else if (s < -1.0) s = -1.0;
	output_buffer[i] = s;
      }
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
