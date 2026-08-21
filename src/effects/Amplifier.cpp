#include "Amplifier.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

using namespace std;

namespace {

// Actual DSP, shared by AmplifierTrackState (a persistent track-tree
// wrapper) and AmplifierVoiceState (an ephemeral per-note wrapper) - see
// EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md for why Amplifier (like every
// effects/ class) needs both.
class AmplifierDsp {
public:
  explicit AmplifierDsp(float gain) : gain_(gain) { }

  // Scales every channel - Main and AuxA/AuxB alike: if the source is too
  // loud or too quiet, its contribution to the reverb/delay bus should
  // scale the same way, not stay at the original level. Returns whether
  // there was anything to scale at all (Main, Aux, or both) - not Main
  // specifically, since an Aux-only input (Send Main = 0) is still
  // genuinely being processed here; the caller feeds this into its own
  // isEffectActive() bookkeeping.
  bool applyEffect(AudioBuffer & input) const {
    bool has_content = input.numberOfChannels() > 0;
    if (has_content) {
      auto data = input.getChannelData(0);
      auto g = TrackState::decibelsToGain(gain_);

      for (int i = 0; i < input.numberOfFrames() * input.numberOfChannels(); i++) {
	data[i] *= g;
      }
    }
    return has_content;
  }

private:
  float gain_;
};

class AmplifierTrackState : public EffectTrackState {
public:
  AmplifierTrackState(const ChannelConfiguration & channel_config, float gain)
    : EffectTrackState(channel_config), dsp_(gain) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  AmplifierDsp dsp_;
};

class AmplifierVoiceState : public EffectVoiceState {
public:
  AmplifierVoiceState(const ChannelConfiguration & channel_config, float gain)
    : EffectVoiceState(channel_config), dsp_(gain) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
  }

private:
  AmplifierDsp dsp_;
};

}

std::unique_ptr<TrackState>
Amplifier::createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const {
  return make_unique<AmplifierTrackState>(channel_config, gain_);
}

std::unique_ptr<VoiceState>
Amplifier::createVoiceState(const ChannelConfiguration & channel_config) const {
  return make_unique<AmplifierVoiceState>(channel_config, gain_);
}

void
Amplifier::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  gain_ = input.getFloat("gain", 0);
}

void
Amplifier::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("gain", gain_);
}
