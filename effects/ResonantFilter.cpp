#include "ResonantFilter.h"

#include "EffectState.h"

#include "MoogVCF.h"
#include "../EnvelopeState.h"
#include "../constants.h"

#include <cassert>

using namespace std;

class ResonantFilterState : public EffectState {
public:
  ResonantFilterState(const ChannelConfiguration & channel_config, const ResonantFilter & filter, const Envelope & envelope, bool use_aftertouch)
    : EffectState(channel_config),
      cut_min_(filter.get_cut_min()),
      cut_max_(filter.get_cut_max()),
      res_(filter.get_res()),
      envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true),
      use_aftertouch_(use_aftertouch)
  { }  
  
  void applyEffect(SampleData & input_data) override {
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();
    auto left_buffer = input_data.getChannelData(0), right_buffer = input_data.getChannelData(1);
    auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;
    
    while (numSamples) {
      size_t blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      float current_cut = (cut_min_ + envelope_state_.getLevel() * aftertouch_value * (cut_max_ - cut_min_)) / (getChannelConfiguration().getAudioOutSampleRate() * 0.5f);

      if (!input_data.isZero() || is_active_) {
	input_data.setNonZero();
	is_active_ = true;
	
	if (numChannels == 1) {
	  left_state_.apply(blockSamples, left_buffer, current_cut, res_);
	} else {
	  left_state_.apply(blockSamples, left_buffer, current_cut, res_);
	  right_state_.apply(blockSamples, right_buffer, current_cut, res_);
	}
      }
      
      left_buffer += blockSamples;
      right_buffer += blockSamples;
      numSamples -= blockSamples;
      envelope_state_.process(blockSamples);      
    }

    setTrackInfo(TrackInfo( is_active_, input_data.isClipping()));
  }

private:
  float cut_min_, cut_max_, res_;
  bool use_aftertouch_;

  MoogVCF<float> left_state_, right_state_;

  EnvelopeState envelope_state_;

  bool is_active_ = false;
};

std::unique_ptr<TrackState>
ResonantFilter::createState(const ChannelConfiguration & config) const {
  return make_unique<ResonantFilterState>(config, *this, envelope_, use_aftertouch_);
}

void
ResonantFilter::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
  
  if (input.has("cut")) {
    cut_min_ = cut_max_ = input.getFloat("cut", 0.0f);
  } else {
    cut_min_ = input.getFloat("cutmin", 0.0f);
    cut_max_ = input.getFloat("cutmax", 0.0f);
  }

  res_ = input.getFloat("res");
  use_aftertouch_ = input.getBool("aftertouch");
  
  envelope_.loadParameters(input);
}

void
ResonantFilter::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  if (cut_min_ == cut_max_) {
    output.set("cut", cut_min_);
  } else {
    output.set("cutmin", cut_min_);
    output.set("cutmax", cut_max_);
  }

  output.set("res", res_);
  output.set("aftertouch", use_aftertouch_);

  envelope_.storeParameters(output);
}
