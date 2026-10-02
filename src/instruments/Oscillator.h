#ifndef _OSCILLATOR_H_
#define _OSCILLATOR_H_

#include "Instrument.h"
#include "WaveformType.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

class Oscillator : public Instrument {
 public:
  explicit Oscillator(WaveformType type) : type_(type) { }

  const char * getElementName() const override { return "oscillator"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

  // What playNote() builds its voice from - NoteMultiplier reads these to
  // render several copies in one OscillatorArrayVoice instead.
  WaveformType getType() const { return type_; }
  float getLevel() const { return level_; }
  float getPulseWidth() const { return pulse_width_; }
  // playNote()'s own scaling of the detune ratio, applied to `detune`.
  float applyHarmonics(float detune) const { return detune * static_cast<float>(harmonic_) / static_cast<float>(subharmonic_); }

 private:
  WaveformType type_;
  float level_ = 1.0f, pulse_width_ = 0.5f;
  // Frequency ratio harmonic/subharmonic relative to the played note.
  int harmonic_ = 1, subharmonic_ = 1;
};

#endif
