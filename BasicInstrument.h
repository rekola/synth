#ifndef _BASICINSTRUMENT_H_
#define _BASICINSTRUMENT_H_

#include "Instrument.h"

enum class WaveformType
  {
   SINE = 1,
   SAW,
   SQUARE,
   NOISE,
  };
  
class BasicInstrument : public Instrument {
 public:  
  explicit BasicInstrument(WaveformType _type) : Instrument(1), type(_type) { }

  std::unique_ptr<InstrumentVoice> createVoice(int _identifier) const;
  
 private:
  WaveformType type;
};

#endif
