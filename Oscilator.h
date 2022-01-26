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
  explicit Oscilator(WaveformType _type) : type(_type) { }

  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(ChannelConfiguration config, int outSampleRate, float azimuth, float frequency, float velocity, float start_phase) const override;

 private:
  WaveformType type;
  int harmonic = 1, subharmonic = 1;
  float level = 1.0f;
};

#endif
