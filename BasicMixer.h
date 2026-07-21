#ifndef _BASICMIXER_H_
#define _BASICMIXER_H_

#include "Mixer.h"
#include "SampleData.h"

#include <cmath>

class BasicMixer : public Mixer {
 public:
  BasicMixer(short _out_channels, int _outSampleRate) : Mixer(_out_channels, _outSampleRate) { }

  void reset() override {
    buffer_.zero();
  }

  // Only ever sums buffer_'s own (fixed, regular-only) channels - via
  // mixNamed() rather than mix(), since `input` may carry SendA/SendB
  // trailing its regular channels (see Mixer.h) which buffer_ never marks
  // present and so silently ignores.
  void accumulate(const SampleData & input) override {
    if (buffer_.numberOfFrames() != input.numberOfFrames()) {
      buffer_ = SampleData(getOutChannels(), input.numberOfFrames());
      buffer_.zero();
    }
    buffer_.mixNamed(input);
  }

  const SampleData & getRawBus() const override { return buffer_; }

  SampleData encode() override {
    SampleData output(getOutChannels(), buffer_.numberOfFrames());
    output.setNonZero();

    for (int j = 0; j < output.numberOfChannels(); j++) {
      auto output_buffer = output.getChannelData(j);
      auto in_buffer = buffer_.getChannelData(j);
      for (int i = 0; i < output.size(); i++) {
	float s = in_buffer[i];
	if (s > 1.0f) s = 1.0f;
	else if (s < -1.0f) s = -1.0f;
	output_buffer[i] = s;
      }
    }
    return output;
  }

private:
  SampleData buffer_;
};

#endif
