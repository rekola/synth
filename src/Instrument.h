#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Track.h"

class InstrumentProvider;

class Instrument : public Track {
public:
  explicit Instrument() : Track(TrackType::INSTRUMENT) { }
  
  virtual void prepare(const InstrumentProvider & provider) { }
      
  void loadParameters(const ParameterSource & input) {
    Track::loadParameters(input);
  
    harmonic_ = input.getInt("harmonic", 1);
    subharmonic_ = input.getInt("subharmonic", 1);  
  }

  void storeParameters(ParameterSource & output) const {
    Track::storeParameters(output);
    
    if (harmonic_ != 1) output.set("harmonic", harmonic_);
    if (subharmonic_ != 1) output.set("subharmonic", subharmonic_);
  }

  int getHarmonic() const { return harmonic_; }
  int getSubharmonic() const { return subharmonic_; }

private:
  int harmonic_ = 1, subharmonic_ = 1;
};

#endif
