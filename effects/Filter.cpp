#include "Filter.h"

#include "MoogVCF.h"

#include "../TrackState.h"
#include "../EnvelopeState.h"

#include "../constants.h"

#include <cassert>

using namespace std;

class FilterState : public TrackState {
public:
  FilterState(const ChannelConfiguration & channel_config, const Filter & filter, const Envelope & envelope, bool use_aftertouch)
    : TrackState(channel_config),
      cut_min_(filter.get_cut_min()),
      cut_max_(filter.get_cut_max()),
      res_(filter.get_res()),
      is_highpass_(filter.get_is_highpass()),
      envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true),
      use_aftertouch_(use_aftertouch)
  { }  
  
  SampleData render(int frames) override {
    auto input_data = TrackState::render(frames);
    
    if ((cut_min_ < 1.0 && cut_max_ < 1.0) || res_ > 0.0) {    
      auto numSamples = input_data.size();
      auto numChannels = input_data.numberOfChannels();
      auto left_buffer = input_data.getChannelData(0), right_buffer = input_data.getChannelData(1);

      auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;
      
      while (numSamples) {
	size_t blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
	float current_cut = cut_min_ + envelope_state_.getLevel() * aftertouch_value * (cut_max_ - cut_min_);
	
	if (numChannels == 1) {
	  left_state_.apply(blockSamples, left_buffer, current_cut, res_, is_highpass_);
	} else {
	  left_state_.apply(blockSamples, left_buffer, current_cut, res_, is_highpass_);
	  right_state_.apply(blockSamples, right_buffer, current_cut, res_, is_highpass_);
	}
	
	left_buffer += blockSamples;
	right_buffer += blockSamples;
	numSamples -= blockSamples;
	envelope_state_.process(blockSamples);
      }
    }

    return input_data;
  }

private:
  float cut_min_, cut_max_, res_;
  bool is_highpass_, use_aftertouch_;

  MoogVCF<float> left_state_, right_state_;

  EnvelopeState envelope_state_;
};

std::unique_ptr<TrackState>
Filter::createState(const ChannelConfiguration & config) const {
  return make_unique<FilterState>(config, *this, envelope_, use_aftertouch_);
}

void
Filter::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
  
  if (input.has("cut")) {
    cut_min_ = cut_max_ = input.getFloat("cut", 0.0f);
  } else {
    cut_min_ = input.getFloat("cutmin", 0.0f);
    cut_max_ = input.getFloat("cutmax", 0.0f);
  }

  res_ = input.getFloat("res");
  use_aftertouch_ = input.getBool("aftertouch");
  is_highpass_ = input.getBool("highpass");
  
  envelope_.loadParameters(input);
}

void
Filter::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  if (cut_min_ == cut_max_) {
    output.set("cut", cut_min_);
  } else {
    output.set("cutmin", cut_min_);
    output.set("cutmax", cut_max_);
  }

  output.set("res", res_);
  output.set("highpass", is_highpass_);
  output.set("aftertouch", use_aftertouch_);

  envelope_.storeParameters(output);
}
