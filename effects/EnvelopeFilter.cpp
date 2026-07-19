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
    // A gain multiply is channel-count-agnostic by construction - applies
    // identically to however many channels are actually present (including
    // ambisonic ones), not just the first two.
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();

    size_t offset = 0;
    while (numSamples) {
      auto blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      auto gain = envelope_state_.getLevel();

      for (int c = 0; c < numChannels; c++) {
	auto buffer = input_data.getChannelData(c) + offset;
	for (decltype(blockSamples) i = 0; i < blockSamples; i++) {
	  buffer[i] *= gain;
	}
      }

      offset += blockSamples;
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
