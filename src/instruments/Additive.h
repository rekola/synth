#ifndef _ADDITIVE_H_
#define _ADDITIVE_H_

#include "Instrument.h"
#include "AdditivePresets.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

#include <string>

// A per-voice sinusoid-bank ("additive synthesis") instrument leaf - each
// partial has its own frequency, amplitude, and independent exponential
// decay rate that rises with frequency (a standard struck-string/plucked-
// string spectral-decay model: higher partials die out faster than the
// fundamental). The actual DSP lives in SinusoidBank.h (the technical
// building block) and AdditiveVoice.h (the InstrumentVoice-integrated
// wrapper, also owning the short attack-transient noise burst); this class
// is only XML parameter parsing/storage plus playNote() construction,
// mirroring Oscillator.h's own shape.
//
// This element's own per-partial decay is a *timbral* effect (partials
// thinning out as the note ages, changing the note's brightness over its
// life) - it is not a substitute for a parent <envelope>'s ADSR, which
// still governs the note's overall amplitude the same as it does for any
// other instrument leaf.
class Additive : public Instrument {
 public:
  explicit Additive() { }

  const char * getElementName() const override { return "additive"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

 private:
  std::string preset_ = "default";
  int partials_ = getAdditivePreset("default").partials;
  float tilt_ = getAdditivePreset("default").tilt;
  // How much velocity brightens the tone: actual tilt used =
  // tilt_ + velocityTilt_ * (velocity - 0.5) - see AdditiveVoice.h's own
  // kReferenceVelocity comment for why 0.5 (velocity is already normalized
  // to [0,1] by the time it reaches playNote(), see Note::
  // getVelocityAsFloat()). A harder hit (velocity above the 0.5 midpoint)
  // makes tilt_ less negative (brighter); a softer one makes it more
  // negative (darker).
  float velocityTilt_ = getAdditivePreset("default").velocityTilt;
  int unisonVoices_ = getAdditivePreset("default").unisonVoices;
  float unisonDetune_ = getAdditivePreset("default").unisonDetune;
  // Stretched-partial coefficient B - 0 means plain harmonic/tuning-matched
  // partials (no stretch). Only affects partials above partialLimit_ when
  // tuningMatched_ is on (a continuous cents-space shift from there) - see
  // SinusoidBank.cpp's additivePartialRatio() for the exact formula and why.
  float inharmonicity_ = getAdditivePreset("default").inharmonicity;
  // alpha_n = decayA_ + decayB_ * f_n^decayP_, nepers/second - see
  // SinusoidBank.h's own doc comment for the exact per-sample envelope
  // this drives (amplitude(t) = amplitude(0) * exp(-alpha_n * t)).
  float decayA_ = getAdditivePreset("default").decayA;
  float decayB_ = getAdditivePreset("default").decayB;
  float decayP_ = getAdditivePreset("default").decayP;
  bool tuningMatched_ = getAdditivePreset("default").tuningMatched;
  int partialLimit_ = getAdditivePreset("default").partialLimit;
  float attackNoiseLevel_ = getAdditivePreset("default").attackNoiseLevel;
  float level_ = 1.0f;
};

#endif
