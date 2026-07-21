#include "Delay.h"

#include "EffectState.h"

#include "../Biquad.h"
#include "../constants.h"

using namespace std;

#define MAX_DELAY	4.0f

class DelayState : public EffectState {
public:
  DelayState(const ChannelConfiguration & channel_config, float delay, float feedback, float delaymix)
    : EffectState(channel_config),
      delay_(delay),
      feedback_(feedback),      
      delaymix_(delaymix),
      delay_buffer_(channel_config, static_cast<int>(MAX_DELAY * getChannelConfiguration().getAudioOutSampleRate()))
  {
    delay_buffer_.zero();
    for (int i = 0; i < getChannelConfiguration().numberOfChannels(); i++) {
      filters_.emplace_back(FilterType::lowpass, lowpass_fc_ / getChannelConfiguration().getAudioOutSampleRate(), 0.707f);
    }
  }
  
protected:
  void applyEffect(SampleData & input) override {
    float delay = delay_;
    if (bpm_lock_) {
      delay *= getChannelConfiguration().getRowDuration(input.getBpm());
    }
    int delay_frames = delay * getChannelConfiguration().getAudioOutSampleRate();
    if (delay_frames > delay_buffer_.numberOfFrames()) delay_frames = delay_buffer_.numberOfFrames();
    
    if (!input.isZero() || isEffectActive()) {
      setEffectActive(true);
      
      auto numSamples = input.size();
      // Regular channels only - filters_/delay_buffer_ are sized once, at
      // construction, from the plain ChannelConfiguration (which stays
      // unaware of SendA/SendB by design, see SampleData.h). input's live
      // numberOfChannels() can be wider whenever a send is present, so
      // indexing filters_/delay_buffer_ with the raw count would run past
      // the end of those fixed-size buffers - the send channels are
      // deliberately left untouched here rather than delayed, since they
      // need to reach the shared reverb/chorus bus unmodified.
      auto numChannels = input.numberOfChannels() - input.sendCount();

      for (int j = 0; j < numChannels; j++) {
	auto input_buffer = input.getChannelData(j);
	auto delay_buffer = delay_buffer_.getChannelData(j);
	auto & filter = filters_[j];
	
	for (int i = 0; i < numSamples; i++) {
	  auto buffer_pos = (delay_pos_ + i) % delay_frames;
	  
	  auto x = input_buffer[i];
	  auto y = delay_buffer[buffer_pos];
	  delay_buffer[buffer_pos] = filter.process(x + y * feedback_);
	  
	  input_buffer[i] = delaymix_ * y + (1 - delaymix_) * x;
	}
      }
      
      delay_pos_ = (delay_pos_ + numSamples) % delay_frames;
    }

    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping()));
  }
  
private:
  float feedback_, delay_, delaymix_, lowpass_fc_ = 5000;

  // delay state
  int delay_size_ = 0;
  int delay_pos_ = 0;
  SampleData delay_buffer_;

  std::vector<Biquad<float>> filters_;
  
  bool is_active_ = false, bpm_lock_ = true;
};

std::unique_ptr<TrackState>
Delay::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DelayState>(channel_config, delay_, feedback_, delaymix_);
}

void
Delay::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
    
  delay_ = input.getFloat("delay");
  feedback_ = input.getFloat("feedback");
  delaymix_ = input.getFloat("mix");
  bpm_lock_ = input.getBool("lock", false);
}

void
Delay::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("delay", delay_);
  output.set("feedback", feedback_);
  output.set("mix", delaymix_);
  output.set("lock", bpm_lock_);
}
