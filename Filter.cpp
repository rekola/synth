#include "Filter.h"

#include "SampleData.h"

#include <cassert>

using namespace std;

class FilterState : public EffectState {
public:
  FilterState(int samplerate, const Filter & filter)
    : EffectState(samplerate), fcut(filter.get_fcut()), fres(filter.get_fres()), is_highpass(filter.get_is_highpass()) { }
  
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

      // lfo.process(1);
    }
  }
  
private:
  float fcut, fres;
  bool is_highpass;
  
  // float lfo_amount = 0, lfo_rate = 0, lfo_phase = 0;
  
  // filter state
  float in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  float out1 = 0, out2 = 0, out3 = 0, out4 = 0;
  
  LFO lfo;
};

std::unique_ptr<EffectState>
Filter::createState(unsigned int outSampleRate) const {
  return make_unique<FilterState>(outSampleRate, *this);
}
