#include "Reverb.h"

#include "MVerb.h"
#include "SampleData.h"

#include "tinyxml2.h"

using namespace std;

class ReverbState : public EffectState {
public:
  ReverbState(unsigned int outSampleRate, ReverbPreset preset)
    : EffectState(outSampleRate), mverb(outSampleRate, int(preset)) {

  }

  void apply(SampleData & input) override {
    auto left_in_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto right_in_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto left_out_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto right_out_ptr = unique_ptr<float[]>(new float[input.size()]);

    auto left_in = left_in_ptr.get(), right_in = right_in_ptr.get();
    auto left_out = left_out_ptr.get(), right_out = right_out_ptr.get();
    
    memset(left_out, 0, input.size() * sizeof(float));
    memset(right_out, 0, input.size() * sizeof(float));

    float * in[2] = { left_in, right_in };
    float * out[2] = { left_out, right_out };
    float * io_data = input.data();
    
    if (input.getChannels() == 2) {
      for (size_t i = 0; i < input.size(); i++) {
	left_in[i] = io_data[2 * i + 0];
	right_in[i] = io_data[2 * i + 1];
      }
      mverb.process(in, out, input.size());
      
      for (size_t i = 0; i < input.size(); i++) {
	io_data[2 * i + 0] = left_out[i];
	io_data[2 * i + 1] = right_out[i];
      }
    } else {
      for (size_t i = 0; i < input.size(); i++) {
	left_in[i] = right_in[i] = io_data[i];
      }

      mverb.process(in, out, input.size());

      for (size_t i = 0; i < input.size(); i++) {
	io_data[i] = left_out[i];
      }
    }
  }

private:
  MVerb<float> mverb;
};

std::unique_ptr<EffectState>
Reverb::createState(unsigned int outSampleRate) const {
  return make_unique<ReverbState>(outSampleRate, preset);
}

void
Reverb::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);
  
  auto preset_text = element.Attribute("preset");
  if (preset_text) {
    if (strcmp(preset_text, "subtle") == 0) preset = ReverbPreset::SUBTLE;
    else if (strcmp(preset_text, "stadium") == 0) preset = ReverbPreset::STADIUM;
    else if (strcmp(preset_text, "cupboard") == 0) preset = ReverbPreset::CUPBOARD;
    else if (strcmp(preset_text, "dark") == 0) preset = ReverbPreset::DARK;
    else if (strcmp(preset_text, "halves") == 0) preset = ReverbPreset::HALVES;
  }
}

void
Reverb::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);  
}
