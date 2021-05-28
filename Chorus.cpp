#include "Chorus.h"

#include "EffectState.h"

using namespace std;

#define CHORUS_MAX_DELAY_SAMPLES 44100

class ChorusState : public EffectState {
public:
  ChorusState(unsigned int outSampleRate, float _delay1, float _delay2) : EffectState(outSampleRate), delay1(_delay1), delay2(_delay2) { }

  void apply(SampleData & input_data) override {
    if (delay1 <= 0) return;

    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    float dphi1 = 2 * M_PI * delay1_mod_freq / getOutSampleRate();
    
    for (size_t i = 0; i < input_data.size(); i++) {
      int delay1_offset = (size_t)((delay1 + delay1_mod_amount * sinf(delay1_phi)) / 1000 * getOutSampleRate());
      float x = buffer[i];
      float y = delaybuf1[(CHORUS_MAX_DELAY_SAMPLES + delc1 - delay1_offset) % CHORUS_MAX_DELAY_SAMPLES];
      
      delaybuf1[delc1] = x + feedback * y;
      buffer[i] += y;
			     
      delc1++;
      if (delc1 > CHORUS_MAX_DELAY_SAMPLES) delc1 = 0;

      delay1_phi += dphi1;
      if (delay1_phi > 2 * M_PI) delay1_phi -= 2 * M_PI;
    }
  }

private:
  float delay1, delay2;
  
  float feedback = 0.0f;
  float delay1_mod_amount = 2.0f, delay1_mod_freq = 2.0f;
  
  // state
  float delay1_phi = 0;
  size_t delc1 = 0, delc2 = 0;
  float delaybuf1[CHORUS_MAX_DELAY_SAMPLES], delaybuf2[CHORUS_MAX_DELAY_SAMPLES];
};

std::unique_ptr<EffectState>
Chorus::createState(unsigned int outSampleRate) const {
  return make_unique<ChorusState>(outSampleRate, delay1, delay2);
}
