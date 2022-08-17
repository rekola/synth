#include "EnvelopeFilter.h"

#include "EnvelopeState.h"
#include "constants.h"

using namespace std;

class EnvelopeFilterState : public TrackState {
public:
  EnvelopeFilterState(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : TrackState(channel_config), envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true) {
      
  }

  SampleData render(int frames) override {
    auto input_data = TrackState::render(frames);
    
    auto buffer = input_data.data();
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();

    while (numSamples) {
      auto blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      auto gain = envelope_state_.getLevel();
      
      for (auto i = 0; i < numChannels * blockSamples; i++) {
	buffer[i] *= gain;
      }
      
      buffer += blockSamples * numChannels;
      numSamples -= blockSamples;
      envelope_state_.process(blockSamples);
    }

    return input_data;
  }
  
  bool isPlaying() const override { return !envelope_state_.isDone(); }

  void stopNote() override {
    // let children play
    envelope_state_.nextSegment(EnvelopeState::SUSTAIN);
  }
  
  void killNote() override {
    TrackState::killNote(); // kill the children too
    envelope_state_.nextSegment(EnvelopeState::DONE);
  }

 protected:
  EnvelopeState envelope_state_;
};

std::unique_ptr<TrackState>
EnvelopeFilter::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<EnvelopeFilterState>(channel_config, envelope_);
}
