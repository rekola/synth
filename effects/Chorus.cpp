#include "Chorus.h"

#include "EffectState.h"

#include <vector>

#define CHORUS_MAX_DELAY_SAMPLES 44100

using namespace std;

class ChorusState : public EffectState {
public:
  ChorusState(const ChannelConfiguration & channel_config, float delay1, float delay2)
    : EffectState(channel_config), delay1_(delay1), delay2_(delay2) { }

  void applyEffect(SampleData & input_data) override {
    if (delay1_ > 0) {
      auto outSampleRate = getChannelConfiguration().getAudioOutSampleRate();
      auto num_channels = input_data.numberOfChannels();
      if (delay_buffers_.size() < static_cast<size_t>(num_channels)) {
	delay_buffers_.resize(num_channels, vector<float>(CHORUS_MAX_DELAY_SAMPLES));
      }
      float dphi1 = 2 * M_PI * delay1_mod_freq_ / outSampleRate;

      // all channels are processed with the same modulation phase and write
      // position so that the stereo image is preserved
      auto phi0 = delay1_phi_;
      auto delc0 = delc1_;

      for (int j = 0; j < num_channels; j++) {
	auto buffer = input_data.getChannelData(j);
	auto & delaybuf = delay_buffers_[j];
	auto phi = phi0;
	auto delc = delc0;

	for (int i = 0; i < input_data.size(); i++) {
	  int delay1_offset = (delay1_ + delay1_mod_amount_ * sinf(phi)) / 1000 * outSampleRate;
	  float x = buffer[i];
	  float y = delaybuf[(CHORUS_MAX_DELAY_SAMPLES + delc - delay1_offset) % CHORUS_MAX_DELAY_SAMPLES];

	  delaybuf[delc] = x + feedback_ * y;
	  buffer[i] += y;

	  delc++;
	  if (delc >= CHORUS_MAX_DELAY_SAMPLES) delc = 0;

	  phi += dphi1;
	  if (phi > 2 * M_PI) phi -= 2 * M_PI;
	}

	delay1_phi_ = phi;
	delc1_ = delc;
      }
    }


    setTrackInfo(TrackInfo( isEffectActive(), input_data.isClipping()));
  }

private:
  float delay1_, delay2_;

  float feedback_ = 0.0f;
  float delay1_mod_amount_ = 2.0f, delay1_mod_freq_ = 2.0f;

  // state
  float delay1_phi_ = 0;
  int delc1_ = 0;
  std::vector<std::vector<float> > delay_buffers_;
};

std::unique_ptr<TrackState>
Chorus::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<ChorusState>(channel_config, delay1_, delay2_);
}

void
Chorus::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  delay1_ = input.getFloat("delay1");
  delay2_ = input.getFloat("delay2");  
}

void
Chorus::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("delay1", delay1_);
  output.set("delay2", delay2_);
}
