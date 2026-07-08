#ifndef _NOISE_H_
#define _NOISE_H_

#include "Instrument.h"

class Noise : public Instrument {
 public:  
  explicit Noise() { }

  const char * getElementName() const override { return "noise"; }
  
  void loadParameters(const ParameterSource & input) override {
    Instrument::loadParameters(input);
    level_ = input.getFloat("level", 1.0f);
  }
  
  void storeParameters(ParameterSource & output) const override {
    Instrument::storeParameters(output);
    output.set("level", level_);
  }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & config, float azimuth, float frequency, float detune, float velocity, float start_phase) const override;

private:
  float level_ = 1.0f;
};

#endif
