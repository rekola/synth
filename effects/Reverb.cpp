#include "Reverb.h"

#include "MVerb.h"
#include "../TrackState.h"

using namespace std;

class ReverbState : public TrackState {
public:
  ReverbState(const ChannelConfiguration & channel_config, ReverbPreset preset)
    : TrackState(channel_config), mverb(channel_config.getAudioOutSampleRate(), int(preset)) {

  }

protected:
  void applyEffect(SampleData & input) override {
    auto left_out_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto right_out_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto left_out = left_out_ptr.get(), right_out = right_out_ptr.get();
    
    memset(left_out, 0, input.size() * sizeof(float));
    memset(right_out, 0, input.size() * sizeof(float));

    float * out[2] = { left_out, right_out };
    
    if (input.numberOfChannels() == 2) {
      float * in[2] = { input.getChannelData(0), input.getChannelData(0) };
      
      mverb.process(in, out, input.size());
      
      auto left_buffer = input.getChannelData(0), right_buffer = input.getChannelData(1);
      for (int i = 0; i < input.size(); i++) {
	left_buffer[i] = left_out[i];
	right_buffer[i] = right_out[i];
      }
    } else {
      float * in[2] = { input.getChannelData(0), input.getChannelData(1) };

      mverb.process(in, out, input.size());

      auto left_buffer = input.getChannelData(0);
      for (int i = 0; i < input.size(); i++) {
	left_buffer[i] = left_out[i];
      }
    }
  }

private:
  MVerb<float> mverb;
};

std::unique_ptr<TrackState>
Reverb::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<ReverbState>(channel_config, preset_);
}

void
Reverb::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
  
  auto preset_text = input.getText("preset");
  if (preset_text == "subtle") preset_ = ReverbPreset::SUBTLE;
  else if (preset_text == "stadium") preset_ = ReverbPreset::STADIUM;
  else if (preset_text == "cupboard") preset_ = ReverbPreset::CUPBOARD;
  else if (preset_text == "dark") preset_ = ReverbPreset::DARK;
  else if (preset_text == "halves") preset_ = ReverbPreset::HALVES;
}

void
Reverb::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("preset", to_string(preset_));
}
