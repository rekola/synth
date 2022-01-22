#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Track.h"

class InstrumentProvider;

class Instrument : public Track {
public:
  explicit Instrument(size_t _num_channels) : Track(INSTRUMENT), num_channels(_num_channels) { }
  
  virtual void prepare(const InstrumentProvider & provider) { }
      
  size_t getNumChannels() const { return num_channels; }
  float getGain() const { return gain; }

  bool isPercussion() const { return is_percussion; }
  void setIsPercussion(bool t) { is_percussion = t; }
  
protected:
  size_t num_channels;
  float gain = 1.0f;
  bool is_percussion = false;
};

#endif
