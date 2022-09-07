#include "Delay.h"

#include "../TrackState.h"

using namespace std;

class DelayState : public TrackState {
public:
  DelayState(const ChannelConfiguration & channel_config, float delay, float fd, float delaymix)
    : TrackState(channel_config), fd_(fd), delaymix_(delaymix) {
    delay_size_ = delay * getChannelConfiguration().getAudioOutSampleRate();
    int channels = getChannelConfiguration().numberOfChannels();
    buffer_ = std::unique_ptr<float[]>(new float[delay_size_ * channels]);
    memset(buffer_.get(), 0, delay_size_ * channels * sizeof(float));
  }
  
protected:
  void applyEffect(SampleData & input) override {
    for (int j = 0; j < input.numberOfChannels(); j++) {
      auto input_buffer = input.getChannelData(j);
      auto delay_buffer = buffer_.get() + j * delay_size_;
    
      for (int i = 0; i < input.size(); i++) {
	auto buffer_pos = (delay_pos_ + i) % delay_size_;
	  
	float x = input_buffer[i];
	float y = delay_buffer[buffer_pos];
	delay_buffer[buffer_pos] = x + y * fd_;
	
	input_buffer[i] = delaymix_ * y + (1 - delaymix_) * x;
      }
    }

    delay_pos_ = (delay_pos_ + input.size()) % delay_size_;
  }
  
private:
  float fd_, delaymix_;

  // delay state
  int delay_size_ = 0;
  int delay_pos_ = 0;
  std::unique_ptr<float[]> buffer_;
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
