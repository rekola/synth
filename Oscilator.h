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
  explicit Oscilator(WaveformType _type) : Instrument(1), type(_type) { }

  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;
  std::unique_ptr<TrackState> playNote(float frequency, float velocity, unsigned int outSampleRate, float start_phase) const override;

 private:
  WaveformType type;
  int harmonic = 1, subharmonic = 1;
  float level = 1.0f;
};

#endif
