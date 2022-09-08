#include "Delay.h"

#include "../TrackState.h"
#include "../Biquad.h"
#include "../constants.h"

using namespace std;

class DelayState : public TrackState {
public:
  DelayState(const ChannelConfiguration & channel_config, float delay, float fd, float delaymix)
    : TrackState(channel_config),
      fd_(fd),
      delaymix_(delaymix),
      delay_buffer_(channel_config, int(delay * getChannelConfiguration().getAudioOutSampleRate()))
  {
    delay_buffer_.zero();
    for (int i = 0; i < getChannelConfiguration().numberOfChannels(); i++) {
      filters_.emplace_back(FilterType::lowpass, lowpass_fc_ / getChannelConfiguration().getAudioOutSampleRate(), 0.707f);
    }
  }
  
protected:
  void applyEffect(SampleData & input) override {
    auto numSamples = input.size();
    auto numChannels = input.numberOfChannels();
    
    for (int j = 0; j < numChannels; j++) {
      auto input_buffer = input.getChannelData(j);
      auto delay_buffer = delay_buffer_.getChannelData(j);
      auto & filter = filters_[j];
      
      for (int i = 0; i < numSamples; i++) {
	auto buffer_pos = (delay_pos_ + i) % delay_buffer_.numberOfFrames();
	
	auto x = input_buffer[i];
	auto y = delay_buffer[buffer_pos];
	delay_buffer[buffer_pos] = filter.process(x + y * fd_);
	
	input_buffer[i] = delaymix_ * y + (1 - delaymix_) * x;
      }
    }
      
    delay_pos_ = (delay_pos_ + numSamples) % delay_buffer_.numberOfFrames();
  }
  
private:
  float fd_, delaymix_, lowpass_fc_ = 5000;

  // delay state
  int delay_size_ = 0;
  int delay_pos_ = 0;
  SampleData delay_buffer_;

  std::vector<Biquad<float>> filters_;
};

std::unique_ptr<TrackState>
Delay::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DelayState>(channel_config, delay_, fd_, delaymix_);
}

void
Delay::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
    
  delay_ = input.getFloat("delay");
  fd_ = input.getFloat("fd");
  delaymix_ = input.getFloat("mix");
}

void
Delay::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("delay", delay_);
  output.set("fd", fd_);
  output.set("mix", delaymix_);
}
