#include "Tremolo.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

using namespace std;

namespace {

// Actual DSP, shared by TremoloTrackState and TremoloVoiceState - see
// EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md. `aftertouch_value` is passed in
// already resolved by the caller - see BiquadFilterDsp/ResonantFilterDsp's
// own doc comment for why (aftertouch never reaches a persistent
// track-tree node).
class TremoloDsp {
public:
  TremoloDsp(const ChannelConfiguration & channel_config, float frequency, float amplitude)
    : frequency_(frequency), amplitude_(amplitude), sample_rate_(channel_config.getAudioOutSampleRate()) { }

  // Modulates every channel - Main and AuxA/AuxB alike: the reverb/delay
  // bus should hear the same amplitude wobble the dry signal does, the
  // same reasoning as Amplifier/EnvelopeFilter/Compressor.
  void applyEffect(AudioBuffer & input, float aftertouch_value) {
    auto numChannels = input.numberOfChannels();
    if (numChannels > 0) {
      auto numSamples = input.size();
      auto step = 2 * M_PI * frequency_ / sample_rate_;

      for (int j = 0; j < numChannels; j++) {
	auto buffer = input.getChannelData(j);
	auto phi = phi_;
	for (int i = 0; i < numSamples; i++, phi += step) {
	  buffer[i] *= 1 + aftertouch_value * amplitude_ * sin(phi);
	}
      }

      phi_ += numSamples * step;
    }
  }

private:
  float frequency_, amplitude_;
  int sample_rate_;

  double phi_ = 0;
};

class TremoloTrackState : public EffectTrackState {
public:
  TremoloTrackState(const ChannelConfiguration & channel_config, float frequency, float amplitude, bool use_aftertouch)
    : EffectTrackState(channel_config), dsp_(channel_config, frequency, amplitude) { (void) use_aftertouch; }

protected:
  // "Active" tracks whether there was anything to modulate at all (Main,
  // Aux, or both) - not Main specifically, since an Aux-only input (Send
  // Main = 0) is still genuinely being processed above. Aftertouch never
  // reaches a persistent track-tree node - always neutral here regardless
  // of use_aftertouch_.
  void applyEffect(AudioBuffer & input) override {
    dsp_.applyEffect(input, 1.0f);
    setTrackInfo(TrackInfo( input.numberOfChannels() > 0, input.isClipping() ));
  }

private:
  TremoloDsp dsp_;
};

class TremoloVoiceState : public EffectVoiceState {
public:
  TremoloVoiceState(const ChannelConfiguration & channel_config, float frequency, float amplitude, bool use_aftertouch)
    : EffectVoiceState(channel_config), dsp_(channel_config, frequency, amplitude), use_aftertouch_(use_aftertouch) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;
    dsp_.applyEffect(input, aftertouch_value);
  }

private:
  TremoloDsp dsp_;
  bool use_aftertouch_;
};

}

std::unique_ptr<TrackState>
Tremolo::createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const {
  return make_unique<TremoloTrackState>(channel_config, frequency_, amplitude_, use_aftertouch_);
}

std::unique_ptr<VoiceState>
Tremolo::createVoiceState(const ChannelConfiguration & channel_config) const {
  return make_unique<TremoloVoiceState>(channel_config, frequency_, amplitude_, use_aftertouch_);
}

void
Tremolo::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  frequency_ = input.getFloat("frequency");
  amplitude_ = input.getFloat("amplitude");
  use_aftertouch_ = input.getBool("aftertouch");
}

void
Tremolo::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("frequency", frequency_);
  output.set("amplitude", amplitude_);
  output.set("aftertouch", use_aftertouch_);
}
