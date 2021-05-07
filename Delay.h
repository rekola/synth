#ifndef _DELAY_H_
#define _DELAY_H_

#include "SampleData.h"
#include "Effect.h"

#include <cassert>

class Delay : public Effect {
 public:
  Delay(int _delay, float _fd, float _delaymix) : delay(_delay), fd(_fd), delaymix(_delaymix) {
  }

  std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const override;

 private:
  int delay;
  float fd, delaymix;
};

#endif
