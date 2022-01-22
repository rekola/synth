#include "Filter.h"

#include "SampleData.h"
#include "TrackState.h"
#include "EnvelopeState.h"

#include "tinyxml2.h"
#include "defaults.h"
#include <cassert>

using namespace std;

class FilterState : public TrackState {
public:
  FilterState(unsigned int _outSampleRate, const Filter & filter, const Envelope & envelope)
    : TrackState(_outSampleRate), fcut_min(filter.get_fcut_min()), fcut_max(filter.get_fcut_max()), fres(filter.get_fres()), is_highpass(filter.get_is_highpass()), envelope_state(_outSampleRate, envelope, 0, 0, true) { }

  void applyAftertouch(float aftertouch) override {
    TrackState::applyAftertouch(aftertouch);

    
  }
  
  void apply(SampleData & input_data) override {
    if (!
	(
	 (fcut_min < 1.0 && fcut_max < 1.0) || fres > 0.0
	 )
	) return;
    
    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    size_t numSamples = input_data.size();
    
    while (numSamples) {
      size_t blockSamples = numSamples > RENDER_EFFECTSAMPLEBLOCK ? RENDER_EFFECTSAMPLEBLOCK : numSamples;

      float current_fcut = fcut_min + envelope_state.getLevel() * (fcut_max - fcut_min);

      for (size_t i = 0; i < blockSamples; i++) {
	float input = buffer[i];
	float si = input;
	float f = current_fcut * 1.16;
	float ff = f * f;
	float fb = fres * (1.0 - 0.15 * ff);
	f = 1 - f;
	
	input -= out4 * fb;
	input *= 0.35013f * ff * ff;
	out1 = input + 0.3 * in1 + f * out1; // Pole 1
	in1  = input;
	out2 = out1 + 0.3 * in2 + f * out2;  // Pole 2
	in2 = out1;
	out3 = out2 + 0.3 * in3 + f * out3;  // Pole 3
	in3  = out2;
	out4 = out3 + 0.3 * in4 + f * out4;  // Pole 4
	in4  = out3;
	
	if (is_highpass) buffer[i] = si - out4;
	else buffer[i] = out4;
      }

      buffer += blockSamples;
      numSamples -= blockSamples;
      envelope_state.process(blockSamples);
    }
  }

private:
  float fcut_min, fcut_max, fres;
  bool is_highpass;
    
  // filter state
  float in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  float out1 = 0, out2 = 0, out3 = 0, out4 = 0;

  EnvelopeState envelope_state;
};

std::unique_ptr<TrackState>
Filter::createState(unsigned int outSampleRate) const {
  return make_unique<FilterState>(outSampleRate, *this, envelope);
}

void
Filter::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);
  
  auto fcut_text = element.Attribute("fcut");
  if (fcut_text) {
    fcut_min = fcut_max = strtof(fcut_text, nullptr);
  } else {
    auto fcut_min_text = element.Attribute("fcutmin");
    fcut_min = fcut_min_text ? strtof(fcut_min_text, nullptr) : 0.0f;

    auto fcut_max_text = element.Attribute("fcutmax");
    fcut_max = fcut_max_text ? strtof(fcut_max_text, nullptr) : 0.0f;
  }

  auto fres_text = element.Attribute("fres");
  fres = fres_text ? strtof(fres_text, nullptr) : 0.0f;

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

  auto aftertouch_text = element.Attribute("aftertouch");
  aftertouch = aftertouch_text && atoi(aftertouch_text) ? true : false;
}

void
Filter::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);  
}
