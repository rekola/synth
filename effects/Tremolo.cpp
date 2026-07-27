#include "Tremolo.h"

#include "EffectState.h"

using namespace std;

class TremoloState : public EffectState {
public:
  TremoloState(const ChannelConfiguration & channel_config, float frequency, float amplitude, bool use_aftertouch)
    : EffectState(channel_config), frequency_(frequency), amplitude_(amplitude), use_aftertouch_(use_aftertouch) { }

  // Modulates every channel - Main and AuxA/AuxB alike: the reverb/delay
  // bus should hear the same amplitude wobble the dry signal does, the
  // same reasoning as Amplifier/EnvelopeFilter/Compressor.
  void applyEffect(SampleData & input) override {
    auto numChannels = input.numberOfChannels();
    if (numChannels > 0) {
      auto numSamples = input.size();
      auto step = 2 * M_PI * frequency_ / getChannelConfiguration().getAudioOutSampleRate();
      auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;

      for (int j = 0; j < numChannels; j++) {
	auto buffer = input.getChannelData(j);
	auto phi = phi_;
	for (int i = 0; i < numSamples; i++, phi += step) {
	  buffer[i] *= 1 + aftertouch_value * amplitude_ * sin(phi);
	}
      }

      phi_ += numSamples * step;
    }

    // "Active" tracks whether there was anything to modulate at all (Main,
    // Aux, or both) - not Main specifically, since an Aux-only input
    // (Send Main = 0) is still genuinely being processed above.
    setTrackInfo(TrackInfo( numChannels > 0, input.isClipping()));
  }

private:
  float frequency_, amplitude_;
  bool use_aftertouch_;
  
  double phi_ = 0;
};

std::unique_ptr<TrackState>
Tremolo::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<TremoloState>(channel_config, frequency_, amplitude_, use_aftertouch_);
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
