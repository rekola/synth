#ifndef _LFO_H_
#define _LFO_H_

#include "Instrument.h"

class LFO : public Instrument {
 public:
  LFO() { }
  
  const char * getElementName() const override { return "LFO"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & config, float azimuth, float frequency, float detune, float velocity, float start_phase) const override;

private:
  float frequency_ = 1.0f;
  float level_ = 1.0f;
};

#endif
