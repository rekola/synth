#ifndef _ADDITIVE_H_
#define _ADDITIVE_H_

#include "Instrument.h"
#include "AdditiveModel.h"
#include "AdditivePresets.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

#include <string>

// A per-voice sinusoid-bank ("additive synthesis") instrument leaf: a set
// of decaying partials per string, with the spectrum, tuning and decay
// computed by AdditiveModel.h and run by SinusoidBank.h. AdditiveVoice.h is
// the InstrumentVoice-integrated wrapper (one output row per string, each
// placed in space). This class is XML parameter parsing/storage plus
// playNote() construction.
//
// The per-partial decay is a *timbral* effect (partials thinning out as the
// note ages); a parent <envelope>'s ADSR still governs the note's overall
// amplitude as for any other instrument leaf.
class Additive : public Instrument {
 public:
  explicit Additive() { }

  const char * getElementName() const override { return "additive"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

 private:
  std::string preset_ = "default";
  int partials_ = getAdditivePreset("default").partials;
  // Tilt in dB/octave of partial number, plus velocityTilt times
  // (velocity - 0.5): a harder hit is brighter.
  float tilt_ = getAdditivePreset("default").tilt;
  float velocityTilt_ = getAdditivePreset("default").velocityTilt;
  // Strings per note, and the spacing between adjacent ones in cents.
  int unisonVoices_ = getAdditivePreset("default").unisonVoices;
  float unisonDetune_ = getAdditivePreset("default").unisonDetune;
  // Partial n sits at its grid position plus (n-1)*stretch times the
  // fundamental - see AdditiveModel.h.
  float stretch_ = getAdditivePreset("default").stretch;
  // alpha_n = decayA_ + decayB_ * f_n^decayP_, nepers/second.
  float decayA_ = getAdditivePreset("default").decayA;
  float decayB_ = getAdditivePreset("default").decayB;
  float decayP_ = getAdditivePreset("default").decayP;
  bool tuningMatched_ = getAdditivePreset("default").tuningMatched;
  float attackNoiseLevel_ = getAdditivePreset("default").attackNoiseLevel;
  // Degrees of azimuth across the keyboard (bass left, treble right), and
  // between adjacent strings of one key.
  float keyboardSpread_ = getAdditivePreset("default").keyboardSpread;
  float stringSpread_ = getAdditivePreset("default").stringSpread;
  float level_ = 1.0f;
};

#endif
