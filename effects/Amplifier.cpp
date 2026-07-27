#include "Amplifier.h"

#include "EffectState.h"

using namespace std;

class AmplifierState : public EffectState {
public:
  AmplifierState(const ChannelConfiguration & channel_config, float gain)
    : EffectState(channel_config), gain_(gain) {
    
  }

protected:
  // Scales every channel - Main and AuxA/AuxB alike: if the source is too
  // loud or too quiet, its contribution to the reverb/delay bus should
  // scale the same way, not stay at the original level. "Active" tracks
  // whether there's anything to scale at all (Main, Aux, or both) - not
  // Main specifically, since an Aux-only input (Send Main = 0) is still
  // genuinely being processed here.
  void applyEffect(SampleData & input) override {
    bool has_content = input.numberOfChannels() > 0;
    if (has_content) {
      auto data = input.getChannelData(0);
      auto g = decibelsToGain(gain_);

      for (int i = 0; i < input.numberOfFrames() * input.numberOfChannels(); i++) {
	data[i] *= g;
      }
    }

    setEffectActive(has_content);
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  float gain_;
};

std::unique_ptr<TrackState>
Amplifier::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<AmplifierState>(channel_config, gain_);
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
