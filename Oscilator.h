#ifndef _OSCILATOR_H_
#define _OSCILATOR_H_

#include "Instrument.h"
#include "WaveformType.h"

class Oscilator : public Instrument {
 public:  
  explicit Oscilator(WaveformType type) : type_(type) { }

  std::string getElementName() const override { return "oscilator"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & config, float azimuth, float frequency, float velocity, float start_phase) const override;

 private:
  WaveformType type_;
  float level_ = 1.0f;
};

#endif
