#ifndef _TAPEDEGRADATION_H_
#define _TAPEDEGRADATION_H_

#include "Effect.h"
#include "../AmbisonicEncoding.h"
#include "../SphericalPosition.h"
#include "../SendLevels.h"
#include "../NoteCoordinate.h"
#include "../dsp/TapeTransport.h"

// Source-attached tape/media degradation - the degradation belongs to the
// sound source, not the playback chain, so it's modeled as a per-track or
// per-voice effect rather than anything on the shared ambisonic bus.
// Forces its children down to MONO (like Chorus/Distortion,
// getChildChannelConfiguration() below) and runs a single-channel signal
// chain (wow/flutter, dropouts/clicks, hiss, saturation, HF rolloff + head
// bump), then re-encodes the mono result at a real, known position -
// never encodeMonoAsPoint()'s omnidirectional fallback, so the
// degradation stays a genuine point source rather than collapsing to
// center the way Chorus/Distortion deliberately do. Track-attached, the
// position is this instance's own authored
// azimuth_/elevation_/distance_/extent_, mirroring InstrumentTrack's own
// position model. Voice-attached, the position is whatever playNote() was
// actually given, captured via this class's own playNote() override below
// - azimuth_/elevation_/distance_/extent_ are not read in that mode at
// all.
//
// Every parameter here, `preset` included, is XML-only - read once in
// loadParameters(), never live-adjusted.
class TapeDegradation : public Effect {
 public:
  TapeDegradation() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "tapeDegradation"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  ChannelConfiguration getChildChannelConfiguration(const ChannelConfiguration & config) const override { return reduceForEffect(config); }

  // Overridden (not just createVoiceState()) so the note's real position
  // can be captured - createVoiceState() alone never sees it.
  // createVoiceState() above still exists as a defensive
  // fallback for the (currently unreached, per Track.h's own comment on
  // when it's called) case of something invoking it directly rather than
  // going through playNote() - it builds a real TapeDegradationVoiceState
  // too, just with no position known (omnidirectional), rather than
  // silently falling through to Track::createVoiceState()'s inert plain
  // VoiceState.
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune,
                                        float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

 private:
  SphericalPosition getPosition() const { return { azimuth_, elevation_, distance_, extent_ }; }
  TapeTransportParams buildTransportParams() const;

  // Track-attached position only - see the class comment above.
  float azimuth_ = 0.0f, elevation_ = 0.0f, distance_ = 0.0f, extent_ = -1.0f;

  std::string preset_ = "tape";

  float wowRateHz_ = 0.7f, wowDepthCents_ = 8.0f;
  bool wowLocked_ = false;          // Vinyl: fixed 33 1/3rpm wow instead of the random, health-driven kind
  float wowLockedRateHz_ = 0.5556f;
  float flutterRateHz_ = 8.0f, flutterDepthCents_ = 3.0f;
  float healthRateHz_ = 0.2f, healthSensitivity_ = 0.5f;
  float hissLevelDB_ = -48.0f;
  float dropoutRateHz_ = 0.15f, dropoutDepthDB_ = -12.0f, dropoutDurationMs_ = 15.0f;
  float clickRateHz_ = 0.3f, clickGainDB_ = -18.0f;
  float saturationDriveDB_ = 3.0f;
  float lowCutHz_ = 20.0f; // near-inaudible by default - only a narrow-band preset (e.g. Mellotron) raises this
  float hfRolloffHz_ = 12000.0f;
  float headBumpHz_ = 80.0f, headBumpGainDB_ = 3.0f;
  float mix_ = 1.0f;

  // Mellotron-specific - inert (0) for a plain tape player. ampFlutterDepth
  // see dsp/TapeTransport.h. swoopStartCents/swoopTimeMs and
  // droopDepthCents/spinDownMs are the note-on/note-off ends of the same
  // spin-up/spin-down shape (TapeTransportLifecycle, dsp/TapeTransport.h) -
  // spin-down is deliberately its own parameter, not derived from the
  // instrument's own release time (see effects/TapeDegradation.cpp's
  // TapeDegradationDsp::noteOff()): a short release with a short spin-down
  // gives the Mellotron feel, a long release with a late spin-down gives a
  // pad that sags only at the very end. droopDepthCents = 0 gives a clean
  // stop with no pitch bend at all, still a fully faded/drained one.
  float ampFlutterDepth_ = 0.0f;
  float swoopStartCents_ = 0.0f;
  float swoopTimeMs_ = 150.0f;
  float spinDownMs_ = 250.0f;
  float droopDepthCents_ = 0.0f;

  // Vinyl-specific - inert (far below audibility) for every other preset.
  float rumbleLevelDB_ = -100.0f;
  float rumbleHz_ = 80.0f;

  // Disintegration-specific - decayMode false makes decayRatePerMinute's
  // value irrelevant.
  bool decayMode_ = false;
  float decayRatePerMinute_ = 0.0f;

  // Cassette (breathingAmount)/Optical film (hissLevelDependent)-specific -
  // both 0 for every preset that doesn't use them. See dsp/TapeTransport.h's
  // own doc comment on why these four live in TapeTransportParams even
  // though only TapeDegradationDsp itself (not TapeTransportDsp) reads them.
  float breathingAmount_ = 0.0f;
  float hissLevelDependent_ = 0.0f;
  float breathingAttackMs_ = 5.0f;
  float breathingReleaseMs_ = 80.0f;
};

#endif
