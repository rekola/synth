#ifndef _BASICINSTRUMENT_H_
#define _BASICINSTRUMENT_H_

#include "Instrument.h"

#include <cmath>

enum class WaveformType
  {
   SINE = 1,
   SAW,
   SQUARE,
   NOISE,
  };
  
class BasicInstrument : public Instrument {
 public:  
  explicit BasicInstrument(WaveformType _type) : type(_type) { }

  std::shared_ptr<InstrumentVoice> createVoice(int _identifier) const;
  
 private:
  WaveformType type;
  
#if 0
  static void initialize();
  
  static bool is_initialized;
  static float waves[4][WAVESIZE];
#endif
};

#endif
