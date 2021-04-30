#ifndef _REVERB_H_
#define _REVERB_H_

#include "Effect.h"
#include "SampleData.h"
#include "MVerb.h"

class Reverb : public Effect {
 public:
  enum Preset { SUBTLE = 0, STADIUM, CUPBOARD, DARK, HALVES };

  explicit Reverb(int sample_rate, Preset preset) : mverb(sample_rate, preset) {
    
  }

  void apply(SampleData & input) override {
    float * left_in = new float[input.size()];
    float * right_in = new float[input.size()];
    float * left_out = new float[input.size()];
    float * right_out = new float[input.size()];

    memset(left_out, 0, input.size() * sizeof(float));
    memset(right_out, 0, input.size() * sizeof(float));

    float * in[2] = { left_in, right_in };
    float * out[2] = { left_out, right_out };
    float * io_data = input.data();
    
    if (input.getChannels() == 2) {
      for (size_t i = 0; i < input.size(); i++) {
	left_in[i] = io_data[2 * i + 0];
	right_in[i] = io_data[2 * i + 1];
      }
      mverb.process(in, out, input.size());

      for (size_t i = 0; i < input.size(); i++) {
	io_data[2 * i + 0] = left_out[i];
	io_data[2 * i + 1] = right_out[i];
      }
    } else {
      for (size_t i = 0; i < input.size(); i++) {
	left_in[i] = right_in[i] = io_data[i];
      }

      mverb.process(in, out, input.size());

      for (size_t i = 0; i < input.size(); i++) {
	io_data[i] = left_out[i];
      }
    }
        
    delete[] left_in;
    delete[] right_in;
    delete[] left_out;
    delete[] right_out;
  }

protected:
  MVerb<float> mverb;
};

#endif // SNDFILTER_REVERB__H
