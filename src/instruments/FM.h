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
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

 private:
  float level_ = 1.0f;
  // Modulator frequency / carrier frequency.
  float ratio_ = 1.0f;
  // Peak phase deviation in radians, scaled by velocity.
  float index_ = 1.0f;
  // Time constant (s) of the index's exponential decay; 0 keeps it constant.
  float index_decay_ = 0.0f;
  // Exponent k scaling the index by (middle C / note frequency)^k: 0 keeps it
  // the same on every note, 1 keeps the spectrum's bandwidth fixed in Hz.
  float index_tracking_ = 0.0f;
  // The same for the index decay's time constant: 1 makes it twice as long
  // an octave down.
  float index_decay_tracking_ = 0.0f;
  // Phase deviation (radians) the modulator applies to itself; around pi
  // turns its sine into a near-sawtooth.
  float feedback_ = 0.0f;
  // Pitch offset in cents, for layering detuned unison copies.
  float detune_cents_ = 0.0f;
};

#endif
