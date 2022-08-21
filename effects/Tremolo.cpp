#include "Tremolo.h"

#include "../TrackState.h"

using namespace std;

class TremoloState : public TrackState {
public:
  TremoloState(const ChannelConfiguration & channel_config, float frequency, float amplitude, bool use_aftertouch)
    : TrackState(channel_config), frequency_(frequency), amplitude_(amplitude), use_aftertouch_(use_aftertouch) { }

  SampleData render(int frames) override {
    auto input = TrackState::render(frames);
    
    auto numChannels = input.numberOfChannels();
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

    return input;
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
  Track::loadParameters(input);

  frequency_ = input.getFloat("frequency");
  amplitude_ = input.getFloat("amplitude");
  use_aftertouch_ = input.getBool("aftertouch");
}

void
Tremolo::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("frequency", frequency_);
  output.set("amplitude", amplitude_);
  output.set("aftertouch", use_aftertouch_);
}
