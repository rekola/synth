#include "Delay.h"

#include "EffectState.h"

#define MAX_DELAY_SAMPLES 44100 * 5

using namespace std;

class DelayState : public EffectState {
public:
  DelayState(unsigned int outSampleRate, int _delay, float _fd, float _delaymix)
    : EffectState(outSampleRate), delay(_delay), fd(_fd), delaymix(_delaymix) {
    memset(delaybuf, 0, MAX_DELAY_SAMPLES * sizeof(float));
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
  float fd, delaymix;

  // delay state
  int delc = 0;
  float delaybuf[MAX_DELAY_SAMPLES];
};

std::unique_ptr<EffectState>
Delay::createState(unsigned int outSampleRate) const {
  return make_unique<DelayState>(outSampleRate, delay, fd, delaymix);
}
