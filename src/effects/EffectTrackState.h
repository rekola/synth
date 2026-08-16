#ifndef _EFFECTTRACKSTATE_H_
#define _EFFECTTRACKSTATE_H_

#include "../state/TrackState.h"

// Track-tree half of the per-track-effect state contract - see
// plans/trackstate-voicestate-split.md. Every concrete effect
// (Amplifier/BiquadFilter/Chorus/Compressor/Distortion/EnvelopeFilter/
// ResonantFilter/Tremolo) is genuinely usable both as a persistent,
// always-rendered wrapper in the track tree (e.g. songs/demo11.xml's
// <resonantFilter><distortion><track .../></distortion></resonantFilter>
// directly under <tracks>) and as an ephemeral, per-note wrapper inside an
// instrument definition (the far more common case - almost every song has
// <envelope>/<resonantFilter> etc. inside <instruments>). This class and
// its sibling EffectVoiceState (EffectVoiceState.h) exist so each role
// gets exactly the render()/isActive() shape it needs, while the two
// thin state classes each concrete effect defines (e.g. AmplifierTrackState/
// AmplifierVoiceState in Amplifier.cpp) share their actual DSP through a
// small non-polymorphic helper rather than duplicating it.
class EffectTrackState : public TrackState {
 public:
  explicit EffectTrackState(const ChannelConfiguration & channel_config) : TrackState(channel_config) { }

  AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    auto data = TrackState::render(frames, instruments, context);
    applyEffect(data);
    return data;
  }

  // Track is playing if its child is active or it is active by itself.
  bool isActive() const override {
    return TrackState::isActive() || isEffectActive();
  }

 protected:
  virtual void applyEffect(AudioBuffer & input) = 0;
  void setEffectActive(bool is_effect_active) { is_effect_active_ = is_effect_active; }
  bool isEffectActive() const { return is_effect_active_; }

 private:
  bool is_effect_active_ = false;
};

#endif
