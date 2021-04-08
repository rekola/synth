#ifndef _BASICINSTRUMENT_H_
#define _BASICINSTRUMENT_H_

#include "Instrument.h"
#include "WaveformType.h"

#include <cmath>

#define WAVESIZE 1024

class BasicInstrument : public Instrument {
 public:
  BasicInstrument(WaveformType _type) : type(_type) {
    
  }

  float getSample(float fphase) const override {
    if (!is_initialized) initialize();
    
    long mask = WAVESIZE - 1;
    
    if (type == WaveformType::NOISE2) {
      return ((float)rand() / RAND_MAX) * 2.0 - 1.0;
    } else {
      return waves[int(type)][(long)fphase & mask];
    }
  }
  
 private:
  WaveformType type;

  static void initialize();
  
  static bool is_initialized;
  static float waves[4][WAVESIZE];
};

#endif
