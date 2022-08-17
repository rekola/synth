#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Track.h"

class InstrumentProvider;

class Instrument : public Track {
public:
  explicit Instrument() : Track(TrackType::INSTRUMENT) { }
  
  virtual void prepare(const InstrumentProvider & provider) { }
      
  float getGain() const { return gain; }

  bool isPercussion() const { return is_percussion; }
  void setIsPercussion(bool t) { is_percussion = t; }

  void loadParameters(const ParameterSource & input) {
    Track::loadParameters(input);
  
    harmonic_ = input.getInt("harmonic", 1);
    subharmonic_ = input.getInt("subharmonic", 1);  
  }

  void storeParameters(ParameterSource & output) const {
    Track::storeParameters(output);
    
    output.set("harmonic", harmonic_);
    output.set("subharmonic", subharmonic_);
  }

  int getHarmonic() const { return harmonic_; }
  int getSubharmonic() const { return subharmonic_; }

protected:
  float gain = 1.0f;
  bool is_percussion = false;
  int harmonic_ = 1, subharmonic_ = 1;
};

#endif
