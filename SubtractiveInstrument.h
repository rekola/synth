#ifndef _SUBTRACTIVEINSTRUMENT_H_
#define _SUBTRACTIVEINSTRUMENT_H_

#include "Instrument.h"

enum class WaveformType
  {
   SINE = 1,
   SAW,
   SQUARE,
   NOISE,
  };
  
class SubtractiveInstrument : public Instrument {
 public:  
  explicit SubtractiveInstrument(WaveformType _type) : Instrument(1), type(_type) { }

  std::unique_ptr<InstrumentVoice> createVoice(unsigned int outSampleRate, int identifier) const;
  
 private:
  WaveformType type;
};

#endif
