#include "EnvelopeFilter.h"

#include "EnvelopeState.h"
#include "tinyxml2.h"
#include "defaults.h"

using namespace std;

class EnvelopeFilterState : public TrackState {
public:
  EnvelopeFilterState(unsigned int _outSampleRate, const Envelope & envelope)
    : TrackState(_outSampleRate), envelope_state(_outSampleRate, envelope, 0, 0, true) {
      
  }

  void apply(SampleData & input_data) override {
    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    size_t numSamples = input_data.size();

    while (numSamples) {
      size_t blockSamples = numSamples > RENDER_EFFECTSAMPLEBLOCK ? RENDER_EFFECTSAMPLEBLOCK : numSamples;
      float gain = envelope_state.getLevel();
      
      for (size_t i = 0; i < blockSamples; i++) {
	buffer[i] *= gain;
      }
      
      buffer += blockSamples;
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
EnvelopeFilter::createState(unsigned int outSampleRate) const {
  return make_unique<EnvelopeFilterState>(outSampleRate, envelope);
}

void
EnvelopeFilter::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);

  auto attack_text = element.Attribute("attack");
  envelope.attack = attack_text ? strtof(attack_text, nullptr) : 0.0f;

  auto hold_text = element.Attribute("hold");
  envelope.hold = hold_text ? strtof(hold_text, nullptr) : 0.0f;

  auto decay_text = element.Attribute("decay");
  envelope.decay = decay_text ? strtof(decay_text, nullptr) : 0.0f;

  auto sustain_text = element.Attribute("sustain");
  envelope.sustain = sustain_text ? strtof(sustain_text, nullptr) : 1.0f;

  auto release_text = element.Attribute("release");
  envelope.release = release_text ? strtof(release_text, nullptr) : 0.0f;
}

void
EnvelopeFilter::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);  
}
