#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Track.h"

class InstrumentProvider;

class Instrument : public Track {
public:
  explicit Instrument() : Track(INSTRUMENT) { }
  
  virtual void prepare(const InstrumentProvider & provider) { }
      
  float getGain() const { return gain; }

  bool isPercussion() const { return is_percussion; }
  void setIsPercussion(bool t) { is_percussion = t; }
  
protected:
  float gain = 1.0f;
  bool is_percussion = false;
};

#endif
