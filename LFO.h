#ifndef _LFO_H_
#define _LFO_H_

class LFO {
 public:
  enum Type { NONE = 0, TRIANGLE, SQUARE, SAW, SINE };
  
  LFO() : type_(NONE), samplesUntil_(0), delta_(0.0f) { }
  
  LFO(float delay, float frequency, float outSampleRate)
    : type_(TRIANGLE),
      samplesUntil_((int)(delay * outSampleRate)),
      delta_(4.0f * frequency / outSampleRate)
  {
  }
  
  void process(int blockSamples) {
    if (samplesUntil_ > blockSamples) {
      samplesUntil_ -= blockSamples;
      return;
    }
    level_ += delta_ * blockSamples;
    if (level_ > 1.0f) {
      delta_ = -delta_;
      level_ = 2.0f - level_;
    } else if (level_ < -1.0f) {
      delta_ = -delta_;
      level_ = -2.0f - level_;
    }
  }

  float getLevel() const { return level_; }
  float getDelta() const { return delta_; }

private:
  Type type_;
  int samplesUntil_;
  float delta_;
  float level_ = 0.0f;
};

#endif
