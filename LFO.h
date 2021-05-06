#ifndef _LFO_H_
#define _LFO_H_

class LFO {
 public:
  enum Type { NONE = 0, SAW, SINE };
  
  LFO() : type(NONE), samplesUntil(0), level(0), delta(0) { }
  
  LFO(float delay, float frequency, float outSampleRate)
    : type(SAW),
      samplesUntil((int)(delay * outSampleRate)),
      delta(4.0f * frequency / outSampleRate),
      level(0)
  {
  }
  
  void process(int blockSamples) {
    if (samplesUntil > blockSamples) {
      samplesUntil -= blockSamples;
      return;
    }
    level += delta * blockSamples;
    if (level >  1.0f) {
      delta = -delta;
      level = 2.0f - level;
    } else if (level < -1.0f) {
      delta = -delta;
      level = -2.0f - level;
    }
  }

  float getLevel() const { return level; }
  float getDelta() const { return delta; }

private:
  Type type;
  int samplesUntil;
  float level, delta;
};

#endif
