#ifndef _OSCILATOR_H_
#define _OSCILATOR_H_

#include "Instrument.h"
#include "WaveformType.h"
#include "SphericalPosition.h"

class Oscilator : public Instrument {
 public:
  explicit Oscilator(WaveformType type) : type_(type) { }

  const char * getElementName() const override { return "oscilator"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const override;

 private:
  WaveformType type_;
  float level_ = 1.0f, pulse_width_ = 0.5f;
};

#endif
