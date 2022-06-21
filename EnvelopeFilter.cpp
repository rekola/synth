#include "EnvelopeFilter.h"

#include "EnvelopeState.h"
#include "defaults.h"

using namespace std;

class EnvelopeFilterState : public TrackState {
public:
  EnvelopeFilterState(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : TrackState(channel_config), envelope_state(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true) {
      
  }

  SampleData render(int frames) override {
    auto input_data = TrackState::render(frames);
    
    auto buffer = input_data.data();
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();

    while (numSamples) {
      auto blockSamples = numSamples > RENDER_EFFECTSAMPLEBLOCK ? RENDER_EFFECTSAMPLEBLOCK : numSamples;
      auto gain = envelope_state.getLevel();
      
      for (auto i = 0; i < numChannels * blockSamples; i++) {
	buffer[i] *= gain;
      }
      
      buffer += blockSamples * numChannels;
      numSamples -= blockSamples;
      envelope_state.process(blockSamples);
    }

    return input_data;
  }
  
  bool isPlaying() const override { return !envelope_state.isDone(); }
  bool isReleased() const override { return isPlaying() && envelope_state.isReleased(); }

  void stopNote() override {
    // let children play
    envelope_state.nextSegment(EnvelopeState::SUSTAIN);
  }
  
  void killNote() override {
    // let children play
    envelope_state.nextSegment(EnvelopeState::DONE);
  }

 protected:
  EnvelopeState envelope_state;
};

std::unique_ptr<TrackState>
EnvelopeFilter::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<EnvelopeFilterState>(channel_config, envelope);
}

void
EnvelopeFilter::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  envelope.loadParameters(input);
}

void
EnvelopeFilter::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);
  
  envelope.storeParameters(output);
}
