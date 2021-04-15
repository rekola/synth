#ifndef _FILTER_H_
#define _FILTER_H_

#include "SampleData.h"
#include "Effect.h"

#include <cassert>

class Filter : public Effect {
 public:
 Filter(float _fcut, float _fres, bool is_highpass) : fcut(_fcut), fres(_fres), is_highpass(is_highpass) { }

  void apply(SampleData & input_data) override {
    if (!(fcut < 1.0 || fres > 0.0)) return;

    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    
    for (size_t i = 0; i < input_data.size(); i++) {
      float input = buffer[i];
      float si = input;
      float f = fcut * 1.16;
      float ff = f * f;
      float fb = fres * (1.0 - 0.15 * ff);
      f = 1 - f;
      
      input -= out4 * fb;
      input *= 0.35013 * ff * ff;
      out1 = input + 0.3 * in1 + f * out1; // Pole 1
      in1  = input;
      out2 = out1 + 0.3 * in2 + f * out2;  // Pole 2
      in2 = out1;
      out3 = out2 + 0.3 * in3 + f * out3;  // Pole 3
      in3  = out2;
      out4 = out3 + 0.3 * in4 + f * out4;  // Pole 4
      in4  = out3;
      
      if (is_highpass) buffer[i] = si - out4;
      else buffer[i] = out4;
    }
  }
  
 private:
  float fcut = 1.0, fres = 0.0;
  bool is_highpass;

  // filter state
  float in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  float out1 = 0, out2 = 0, out3 = 0, out4 = 0;
};

#endif
