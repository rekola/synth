#ifndef _FM_H_
#define _FM_H_

#include "Instrument.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

// Two-operator FM: a sine carrier at the note's pitch, phase-modulated by a
// sine modulator at `ratio` times that pitch. The pitch always comes from the
// carrier, so the note stays on the song's tuning; an integer `ratio` puts the
// sidebands on the carrier's harmonic series.
class FM : public Instrument {
 public:
  explicit FM() { }

  const char * getElementName() const override { return "fm"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

 private:
  float level_ = 1.0f;
  // Modulator frequency / carrier frequency.
  float ratio_ = 1.0f;
  // Peak phase deviation in radians, scaled by velocity.
  float index_ = 1.0f;
  // Time constant (s) of the index's exponential decay; 0 keeps it constant.
  float index_decay_ = 0.0f;
};

#endif
