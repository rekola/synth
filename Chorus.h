#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Effect.h"

class Chorus : public Effect {
 public:
  Chorus(float _delay1, float _delay2) : delay1(_delay1), delay2(_delay2) { }

  std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const override;

 private:
  float delay1, delay2; // ms
};

#endif
