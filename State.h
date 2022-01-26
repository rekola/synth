#ifndef _STATE_H_
#define _STATE_H_

class State {
 public:
  explicit State(int outSampleRate) : outSampleRate_(outSampleRate) { }
  virtual ~State() { }

  int getOutSampleRate() const { return outSampleRate_; }
  
 private:
  int outSampleRate_;
};

#endif
