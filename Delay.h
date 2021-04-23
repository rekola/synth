#ifndef _DELAY_H_
#define _DELAY_H_

#include "SampleData.h"
#include "Effect.h"

#include <cassert>

#define MAXDELAYSAMPLES 44100 * 5

class Delay : public Effect {
 public:
  Delay(int _delay, float _fd, float _delaymix) : delay(_delay), fd(_fd), delaymix(_delaymix) {
    memset(delaybuf, 0, MAXDELAYSAMPLES * sizeof(float));
  }

  void apply(SampleData & input_data) override {
    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    
    for (size_t i = 0; i < input_data.size(); i++) {
      float x = buffer[i];
      float y = delaybuf[delc];
    
      delaybuf[delc++] = x + y * fd;
      if (delc >= delay) delc = 0;
    
      buffer[i] += delaymix * y;
    }    
  }
  
 private:
  int delay;
  float fd;
  float delaymix;
  
  // delay state
  int delc = 0;
  float delaybuf[MAXDELAYSAMPLES];
};

#endif
