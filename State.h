#ifndef _STATE_H_
#define _STATE_H_

class State {
 public:
 State(unsigned int _outSampleRate) : outSampleRate(_outSampleRate) { }
  virtual ~State() { }

  unsigned int getOutSampleRate() const { return outSampleRate; }

 private:
  unsigned int outSampleRate;
};

#endif
