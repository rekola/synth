#include "Chorus.h"

#include "../TrackState.h"

#define CHORUS_MAX_DELAY_SAMPLES 44100

using namespace std;

class ChorusState : public TrackState {
public:
  ChorusState(const ChannelConfiguration & channel_config, float delay1, float delay2)
    : TrackState(channel_config), delay1_(delay1), delay2_(delay2) { }

  SampleData render(int frames) override {
    auto input_data = TrackState::render(frames);
    
    if (delay1_ > 0) {
      assert(input_data.numberOfChannels() == 1);

      auto outSampleRate = getChannelConfiguration().getAudioOutSampleRate();
      auto buffer = input_data.getChannelData(0);
      float dphi1 = 2 * M_PI * delay1_mod_freq_ / outSampleRate;
      
      for (int i = 0; i < input_data.size(); i++) {
	int delay1_offset = (delay1_ + delay1_mod_amount_ * sinf(delay1_phi_)) / 1000 * outSampleRate;
	float x = buffer[i];
	float y = delaybuf1_[(CHORUS_MAX_DELAY_SAMPLES + delc1_ - delay1_offset) % CHORUS_MAX_DELAY_SAMPLES];
	
	delaybuf1_[delc1_] = x + feedback_ * y;
	buffer[i] += y;
	
	delc1_++;
	if (delc1_ > CHORUS_MAX_DELAY_SAMPLES) delc1_ = 0;
	
	delay1_phi_ += dphi1;
	if (delay1_phi_ > 2 * M_PI) delay1_phi_ -= 2 * M_PI;
      }
    }
    
    return input_data;
  }

private:
  float delay1_, delay2_;
  
  float feedback_ = 0.0f;
  float delay1_mod_amount_ = 2.0f, delay1_mod_freq_ = 2.0f;
  
  // state
  float delay1_phi_ = 0;
  int delc1_ = 0, delc2_ = 0;
  float delaybuf1_[CHORUS_MAX_DELAY_SAMPLES], delaybuf2_[CHORUS_MAX_DELAY_SAMPLES];
};

std::unique_ptr<TrackState>
Chorus::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<ChorusState>(channel_config, delay1_, delay2_);
}

void
Chorus::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  delay1_ = input.getFloat("delay1");
  delay2_ = input.getFloat("delay2");  
}

void
Chorus::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("delay1", delay1_);
  output.set("delay2", delay2_);
}
