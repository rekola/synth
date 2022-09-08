#include "BiquadFilter.h"

#include "EffectState.h"

#include "../Biquad.h"
#include "../TrackState.h"
#include "../EnvelopeState.h"

#include "../constants.h"

#include <cassert>

using namespace std;

class BiquadFilterState : public EffectState {
public:
  BiquadFilterState(const ChannelConfiguration & channel_config, FilterType type, float fc, float Q, float peakGainDB, const Envelope & envelope, bool use_aftertouch)
    : EffectState(channel_config),
      left_state_(type, fc, Q, peakGainDB),
      right_state_(type, fc, Q, peakGainDB),
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
      // float current_cut = cut_min_ + envelope_state_.getLevel() * aftertouch_value * (cut_max_ - cut_min_);
	
      if (numChannels == 1) {
	left_state_.apply(blockSamples, left_buffer);
      } else {
	left_state_.apply(blockSamples, left_buffer);
	right_state_.apply(blockSamples, right_buffer);
      }
      
      left_buffer += blockSamples;
      right_buffer += blockSamples;
      numSamples -= blockSamples;
      
      envelope_state_.process(blockSamples);
    }
  }

private:
  bool use_aftertouch_;
  Biquad<double> left_state_, right_state_;
  EnvelopeState envelope_state_;
};

std::unique_ptr<TrackState>
BiquadFilter::createState(const ChannelConfiguration & config) const {
  return make_unique<BiquadFilterState>(config, type_, fc_ / config.getAudioOutSampleRate(), Q_, peakGainDB_, envelope_, use_aftertouch_);
}

void
BiquadFilter::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
  
  auto type_text = input.getText("type");
  if (type_text == "lowpass") type_ = FilterType::lowpass;
  else if (type_text == "highpass") type_ = FilterType::highpass;
  else if (type_text == "bandpass") type_ = FilterType::bandpass;
  else if (type_text == "notch") type_ = FilterType::notch;
  else if (type_text == "peak") type_ = FilterType::peak;
  else if (type_text == "lowshelf") type_ = FilterType::lowshelf;
  else if (type_text == "highshelf") type_ = FilterType::highshelf;
  else type_ = FilterType::lowpass;

  fc_ = input.getFloat("fc");
  Q_ = input.getFloat("Q");
  peakGainDB_ = input.getFloat("peakGainDB");
  use_aftertouch_ = input.getBool("aftertouch");
  
  envelope_.loadParameters(input);
}

void
BiquadFilter::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("type", to_string(type_));
  output.set("fc", fc_);
  output.set("Q", Q_);
  output.set("aftertouch", use_aftertouch_);
  
  if (type_ == FilterType::peak || type_ == FilterType::lowshelf || type_ == FilterType::highshelf) {
    output.set("peakGainDB", peakGainDB_);
  }

  envelope_.storeParameters(output);
}
