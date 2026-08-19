#ifndef _NOISE_H_
#define _NOISE_H_

#include "Instrument.h"
#include "../ambisonic/SphericalPosition.h"
#include "NoiseColor.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

class Noise : public Instrument {
 public:
  explicit Noise() { }

  const char * getElementName() const override { return "noise"; }

  void loadParameters(const ParameterSource & input) override {
    Instrument::loadParameters(input);
    level_ = input.getFloat("level", 1.0f);
    auto color_text = input.getText("color", "white");
    color_ = (color_text == "pink") ? NoiseColor::PINK : NoiseColor::WHITE;
  }

  void storeParameters(ParameterSource & output) const override {
    Instrument::storeParameters(output);
    output.set("level", level_);
    output.set("color", to_string(color_));
  }

  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

private:
  float level_ = 1.0f;
  NoiseColor color_ = NoiseColor::WHITE;
};

#endif
