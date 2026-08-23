#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "MonoEffect.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

// Needs real mono input from its children (MonoEffect::
// getChildChannelConfiguration()), same as Distortion/TapeDegradation -
// but unlike them, Chorus's whole character is stereo *width*, which a
// single re-encoded point would throw away. So its own DSP (ChorusDsp,
// Chorus.cpp) duplicates the reduced mono signal into a decorrelated pair
// (ChorusEngine, decorrelate=true - the same technique SoundFontVoice's
// own per-region chorus already uses) and encodes the two results as two
// point sources spread around this effect's own position
// (encodeDecorrelatedPairAsSpreadPoint(), AmbisonicEncoding.h) rather than
// one - unless the parent only wants MONO output (nested under another
// mono-reducing effect, or a non-ambisonic song), in which case it falls
// back to a single non-decorrelated channel, same as before this class
// existed - see ChorusDsp's own comment for exactly where that's decided.
// Track-attached, the position is this instance's own authored
// MonoEffect::getPosition(); voice-attached, whatever playNote() was
// actually given, captured via this class's own playNote() override below -
// same shape as TapeDegradation/Distortion's identical need.
class Chorus : public MonoEffect {
 public:
  Chorus() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "chorus"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Overridden (not just createVoiceState()) so the note's real position
  // can be captured - createVoiceState() alone never sees it. Mirrors
  // TapeDegradation::playNote()/Distortion::playNote() exactly.
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune,
                                        float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

 private:
  int voices_ = 3;
  float rate_ = 0.5f;   // Hz
  float delay_ = 15.0f; // ms, center delay
  float depth_ = 4.0f;  // ms, modulation depth
  float mix_ = 0.5f;
};

#endif
