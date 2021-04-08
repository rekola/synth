#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

enum class WaveformType
  {
   SINE = 0,
   SAW,
   SQUARE,
   NOISE, // metallic noise
   NOISE2 // real noise
  };

class Instrument {
public:
  Instrument() { }
  
  WaveformType type;  
};

#endif
