#ifndef _LFO_H_
#define _LFO_H_

static inline float tsf_cents2Hertz(float cents) { return 8.176f * powf(2.0f, cents / 1200.0f); }

class LFO {
 public:
  enum Type { SAW = 1, SINE };
  
  LFO(Type _type = SAW) : type(_type), samplesUntil(0), level(0), delta(0) { }
  LFO(float delay, int freqCents, float outSampleRate)
    : type(SAW),
      samplesUntil((int)(delay * outSampleRate)),
      delta(4.0f * tsf_cents2Hertz((float)freqCents) / outSampleRate),
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
