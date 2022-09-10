#include "Downmix.h"

#include "../TrackState.h"

using namespace std;

class DownmixState : public TrackState {
public:
  DownmixState(const ChannelConfiguration & channel_config) : TrackState(channel_config) { }

  SampleData render(int frames) override {
    return downmix(TrackState::render(frames));
  }
  
  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    return downmix(TrackState::render(frames, instruments, context));    
  }

protected:
  SampleData downmix(const SampleData & input) {
    auto num_channels = input.numberOfChannels();
    
    if (num_channels != 2) {
      setTrackInfo(TrackInfo( true, input.isClipping() ));

      return input;
    } else {
      auto left_input = input.getChannelData(0), right_input = input.getChannelData(1);
      auto num_samples = input.size();

      SampleData output(1, num_samples, input.isSolo());
      if (!input.isZero()) output.setNonZero();
      
      auto output_data = output.getChannelData(0);
    
      for (size_t i = 0; i < num_samples; i++) {
	output_data[i] = (left_input[i] + right_input[i]) / 2.0f;
      }

      setTrackInfo(TrackInfo( true, output.isClipping() ));

      return output;
    }    
  }
};

std::unique_ptr<TrackState>
Downmix::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DownmixState>(channel_config);
}
