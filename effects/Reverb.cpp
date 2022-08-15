#include "Reverb.h"

#include "MVerb.h"
#include "SampleData.h"
#include "TrackState.h"

using namespace std;

class ReverbState : public TrackState {
public:
  ReverbState(const ChannelConfiguration & channel_config, ReverbPreset preset)
    : TrackState(channel_config), mverb(channel_config.getAudioOutSampleRate(), int(preset)) {

  }

  SampleData render(int frames) override {
    auto input = TrackState::render(frames);
    
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
    
    if (input.numberOfChannels() == 2) {
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

    return input;
  }

private:
  MVerb<float> mverb;
};

std::unique_ptr<TrackState>
Reverb::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<ReverbState>(channel_config, preset);
}

void
Reverb::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
  
  auto preset_text = input.getText("preset");
  if (preset_text == "subtle") preset = ReverbPreset::SUBTLE;
  else if (preset_text == "stadium") preset = ReverbPreset::STADIUM;
  else if (preset_text == "cupboard") preset = ReverbPreset::CUPBOARD;
  else if (preset_text == "dark") preset = ReverbPreset::DARK;
  else if (preset_text == "halves") preset = ReverbPreset::HALVES;
}

void
Reverb::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("preset", to_string(preset));
}
