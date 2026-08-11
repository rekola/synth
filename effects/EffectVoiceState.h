#ifndef _EFFECTVOICESTATE_H_
#define _EFFECTVOICESTATE_H_

#include "../VoiceState.h"

// Voice-chain half of the per-track-effect state contract - see
// EffectTrackState.h (its track-tree sibling) and
// plans/trackstate-voicestate-split.md for why every concrete effect needs
// both.
class EffectVoiceState : public VoiceState {
 public:
  explicit EffectVoiceState(const ChannelConfiguration & channel_config) : VoiceState(channel_config) { }

  AudioBuffer render(int frames) override {
    auto data = VoiceState::render(frames);
    applyEffect(data);
    return data;
  }

  // Voice chain is playing if its child is active or it is active by
  // itself (e.g. EnvelopeFilterVoiceState's own envelope still releasing
  // after the wrapped leaf voice has already gone silent).
  bool isActive() const override {
    return VoiceState::isActive() || isEffectActive();
  }

 protected:
  virtual void applyEffect(AudioBuffer & input) = 0;
  void setEffectActive(bool is_effect_active) { is_effect_active_ = is_effect_active; }
  bool isEffectActive() const { return is_effect_active_; }

 private:
  bool is_effect_active_ = false;
};

#endif
