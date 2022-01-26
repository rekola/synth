#include "EnvelopeFilter.h"

#include "EnvelopeState.h"
#include "defaults.h"

using namespace std;

class EnvelopeFilterState : public TrackState {
public:
  EnvelopeFilterState(ChannelConfiguration _channel_config, int _outSampleRate, const Envelope & envelope)
    : TrackState(_channel_config, _outSampleRate), envelope_state(_outSampleRate, envelope, 0, 0, true) {
      
  }

  void apply(SampleData & input_data) override {
    float * buffer = input_data.data();
    size_t numSamples = input_data.size();
    size_t numChannels = input_data.getChannels();

    while (numSamples) {
      size_t blockSamples = numSamples > RENDER_EFFECTSAMPLEBLOCK ? RENDER_EFFECTSAMPLEBLOCK : numSamples;
      float gain = envelope_state.getLevel();
      
      for (size_t i = 0; i < numChannels * blockSamples; i++) {
	buffer[i] *= gain;
      }
      
      buffer += blockSamples * numChannels;
      numSamples -= blockSamples;
      envelope_state.process(blockSamples);
    }
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
EnvelopeFilter::createState(ChannelConfiguration channel_config, int outSampleRate) const {
  return make_unique<EnvelopeFilterState>(channel_config, outSampleRate, envelope);
}

void
EnvelopeFilter::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  envelope.loadParameters(input);
}

void
EnvelopeFilter::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);
  envelope.storeParameters(output);
}
