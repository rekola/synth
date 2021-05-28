#ifndef _DELAY_H_
#define _DELAY_H_

#include "Effect.h"

class Delay : public Effect {
 public:
  Delay(int _delay = 0.0f, float _fd = 0.0f, float _delaymix = 0.0f) : delay(_delay), fd(_fd), delaymix(_delaymix) {
  }

  std::unique_ptr<TrackState> createState(unsigned int outSamplerate) const override;
  std::string getElementName() const override { return "delay"; }

 private:
  int delay;
  float fd, delaymix;
};

#endif
