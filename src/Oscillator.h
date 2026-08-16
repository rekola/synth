#ifndef _OSCILLATOR_H_
#define _OSCILLATOR_H_

#include "Instrument.h"
#include "WaveformType.h"
#include "SphericalPosition.h"
#include "SendLevels.h"
#include "NoteCoordinate.h"

class Oscillator : public Instrument {
 public:
  explicit Oscillator(WaveformType type) : type_(type) { }

  const char * getElementName() const override { return "oscillator"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

 private:
  WaveformType type_;
  float level_ = 1.0f, pulse_width_ = 0.5f;
};

#endif
