#ifndef _OSCILATOR_H_
#define _OSCILATOR_H_

#include "Instrument.h"

enum class WaveformType
  {
   SINE = 1,
   SAW,
   TRIANGLE,
   SQUARE,
   NOISE,
  };

static inline const std::string to_string(WaveformType type) {
  switch (type) {
  case WaveformType::SINE: return "sine";
  case WaveformType::SAW: return "saw";
  case WaveformType::TRIANGLE: return "triangle";
  case WaveformType::SQUARE: return "square";
  case WaveformType::NOISE: return "noise";
  default: return "";
  }
}

class Oscilator : public Instrument {
 public:  
  explicit Oscilator(WaveformType type) : type_(type) { }

  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & config, float azimuth, float frequency, float velocity, float start_phase) const override;

 private:
  WaveformType type_;
  int harmonic_ = 1, subharmonic_ = 1;
  float level_ = 1.0f;
};

#endif
