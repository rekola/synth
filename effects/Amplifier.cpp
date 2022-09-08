#include "Amplifier.h"

#include "EffectState.h"

using namespace std;

class AmplifierState : public EffectState {
public:
  AmplifierState(const ChannelConfiguration & channel_config, float gain)
    : EffectState(channel_config), gain_(gain) {
    
  }

protected:
  void applyEffect(SampleData & input) override {
    auto data = input.getChannelData(0);
    auto g = decibelsToGain(gain_);
    
    for (int i = 0; i < input.numberOfFrames() * input.numberOfChannels(); i++) {
      data[i] *= g;
    }
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
  Track::loadParameters(input);
  
  gain_ = input.getFloat("gain", 0);  
}

void
Amplifier::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("gain", gain_);
}
