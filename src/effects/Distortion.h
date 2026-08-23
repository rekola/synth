#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "MonoEffect.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

enum class DistortionType { HARD_CLIP = 1, SOFT_CLIP, BITCRUSH, TANH };

static inline const std::string to_string(DistortionType type) {
  switch (type) {
  case DistortionType::HARD_CLIP: return "hardclip";
  case DistortionType::SOFT_CLIP: return "softclip";
  case DistortionType::BITCRUSH: return "bitcrush";
  case DistortionType::TANH: return "tanh";
  default: return "";
  }
}

// Each of these waveshapers is a nonlinear, per-channel-independent
// function - unlike a uniform gain multiply, that does NOT commute with
// linear FOA encoding (distorting each ambisonic channel independently and
// decoding gives a different, wrong result vs. distorting the pre-encode
// mono signal and encoding - see MonoEffect.h's own class comment for why
// that's the actual dividing line, not "is it a filter"). Needs real
// mono input (MonoEffect::getChildChannelConfiguration()), then re-encodes
// at a real point - Track-attached, its own authored
// MonoEffect::getPosition(); voice-attached, whatever playNote() was
// actually given, captured via this class's own playNote() override below -
// same shape as TapeDegradation's identical need (see its own class
// comment), sharing the single-point encodeMonoEffectAsPoint()
// (AmbisonicEncoding.h) rather than the flat, undirected
// encodeMonoAsPoint() this class used before MonoEffect existed.
class Distortion : public MonoEffect {
 public:
  Distortion() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "distortion"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Overridden (not just createVoiceState()) so the note's real position
  // can be captured - createVoiceState() alone never sees it. Mirrors
  // TapeDegradation::playNote() exactly - see its own doc comment.
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune,
                                        float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

 private:
  DistortionType type_ { DistortionType::HARD_CLIP };
  float param_ = 1.0f, drymix_ = 0.0f;
  float drive_ = 1.0f;
};

#endif
