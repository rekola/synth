#ifndef _BASICINSTRUMENT_H_
#define _BASICINSTRUMENT_H_

#include "Instrument.h"

#define WAVESIZE 1024

#include <cmath>

class BasicInstrument : public Instrument {
 public:
  enum WaveformType
  {
    SINE = 0,
      SAW,
      SQUARE,
      NOISE, // metallic noise
      NOISE2 // real noise
      };
  
  explicit BasicInstrument(WaveformType _type) : type(_type) {
    
  }

  float getSample(InstrumentVoice & voice) const override {
    if (!is_initialized) initialize();
    
    long mask = WAVESIZE - 1;
    
    if (type == WaveformType::NOISE2) {
      return ((float)rand() / RAND_MAX) * 2.0 - 1.0;
    } else {
      return waves[int(type)][(long)(voice.getFphase() * WAVESIZE / 44100.0f) & mask];
    }
  }
  
 private:
  WaveformType type;

  static void initialize();
  
  static bool is_initialized;
  static float waves[4][WAVESIZE];
};

#endif
