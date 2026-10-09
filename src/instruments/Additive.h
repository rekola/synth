#ifndef _ADDITIVE_H_
#define _ADDITIVE_H_

#include "Instrument.h"
#include "AdditiveModel.h"
#include "AdditivePresets.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

#include <string>
#include <vector>

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
  Additive();

  const char * getElementName() const override { return "additive"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

 private:
  std::string preset_ = "default";
  int partials_ = 0;
  float stretch_ = 0.0f;
  bool tuningMatched_ = true;
  // Strings per note and the spacing between adjacent ones in cents.
  int unisonVoices_ = 1;
  float unisonDetune_ = 1.0f;
  // A list of mode frequency ratios replacing the harmonic series, e.g. for
  // a bar; empty for a string.
  std::string modes_;
  // "hammer" or "pluck", the strike or pluck point as a fraction of the
  // string, and the lowpass corners that give the excitation its brightness.
  std::string excitation_ = "hammer";
  float strike_ = 0.125f;
  float hammerCutoff_ = 0.0f, hammerTracking_ = 0.0f, hammerVelocity_ = 0.0f;
  float pluckCutoff_ = 0.0f;
  float partialFloor_ = -60.0f;
  // alpha_n = (decayA_ + decayB_ * f_n^decayP_) * (f0 / middle C)^decayTracking_,
  // nepers/second, with the outer strings (1 +- decaySpread_) times that.
  float decayA_ = 0.0f, decayB_ = 0.0f, decayP_ = 1.0f;
  float decayTracking_ = 0.0f, decaySpread_ = 0.0f;
  // The body table's level and its exponent against the key, and the degrees of arc its modes are spread over.
  float thump_ = 0.0f, thumpTracking_ = 0.0f, thumpWidth_ = 0.0f;
  std::vector<BodyMode> body_;
  // Degrees of azimuth across the keyboard (bass left, treble right), and
  // between adjacent strings of one key.
  float keyboardSpread_ = 0.0f, stringSpread_ = 0.0f;
  float level_ = 1.0f;
};

#endif
