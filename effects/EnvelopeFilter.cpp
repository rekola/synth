#include "EnvelopeFilter.h"

#include "EffectState.h"
#include "../EnvelopeState.h"
#include "../constants.h"

using namespace std;

class EnvelopeFilterState : public EffectState {
public:
  EnvelopeFilterState(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : EffectState(channel_config), envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true) {
      
  }

  void applyEffect(SampleData & input_data) override {
    auto left_buffer = input_data.getChannelData(0), right_buffer = input_data.getChannelData(1);
    
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();

    while (numSamples) {
      auto blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      auto gain = envelope_state_.getLevel();
      
      if (numChannels == 1) {
	for (auto i = 0; i < blockSamples; i++) {
	  left_buffer[i] *= gain;
	}
	left_buffer += blockSamples * numChannels;
      } else {
	for (auto i = 0; i < blockSamples; i++) {
	  left_buffer[i] *= gain;
	  right_buffer[i] *= gain;
	}
	left_buffer += blockSamples;
	right_buffer += blockSamples;
      }
      
      numSamples -= blockSamples;
      envelope_state_.process(blockSamples);
    }
  }
  
  bool isActive() const override { return !envelope_state_.isDone(); }

  float getOwnLoudnessFactor() const override { return envelope_state_.getLevel(); }

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
