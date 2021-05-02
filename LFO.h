#ifndef _LFO_H_
#define _LFO_H_

static inline float tsf_cents2Hertz(float cents) { return 8.176f * powf(2.0f, cents / 1200.0f); }

class LFO {
 public:
  LFO() : samplesUntil(0), level(0), delta(0) { }
  LFO(float delay, int freqCents, float outSampleRate)
    : samplesUntil((int)(delay * outSampleRate)),
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

  int samplesUntil;
  float level, delta;
};

#endif
