#include "Downmix.h"

#include "../TrackState.h"

using namespace std;

class DownmixState : public TrackState {
public:
  DownmixState(const ChannelConfiguration & channel_config) : TrackState(channel_config) { }

  SampleData render(int frames) override {
    auto input = TrackState::render(frames);
    
    auto input_data = input.data();
    auto num_channels = input.numberOfChannels();
    auto num_samples = input.size();

    SampleData output(1, num_samples, input.isSolo());
    auto output_data = output.data();
    
    for (size_t i = 0; i < num_samples; i++) {
      auto v = 0.0f;
      for (size_t j = 0; j < num_channels; j++) {
	v += input_data[num_channels * i + j];
      }
      output_data[i] = v;
    }
    
    return input;
  }
};

std::unique_ptr<TrackState>
Downmix::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DownmixState>(channel_config);
}
