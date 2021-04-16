#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Effect.h"

#define CHORUS_MAX_DELAY_SAMPLES 44100

class Chorus : public Effect {
 public:
  Chorus(float _delay1, float _delay2) : delay1(_delay1), delay2(_delay2) { }

  void apply(SampleData & input_data) override {
    if (delay1 <= 0) return;

    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    float dphi1 = M_PI * delay1_mod_freq / 22050;
    
    for (size_t i = 0; i < input_data.size(); i++) {
      int delay1_offset = (size_t)((delay1 + delay1_mod_amount * sinf(delay1_phi)) / 1000 * 44100);
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
  float feedback = 0.0f;
  float delay1, delay2; // ms
  float delay1_mod_amount = 2.0f, delay1_mod_freq = 2.0f;
  
  // state
  float delay1_phi = 0;
  size_t delc1 = 0, delc2 = 0;
  float delaybuf1[CHORUS_MAX_DELAY_SAMPLES], delaybuf2[CHORUS_MAX_DELAY_SAMPLES];
};

#endif
