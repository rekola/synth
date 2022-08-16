#include "Filter.h"

#include "../TrackState.h"
#include "../EnvelopeState.h"

#include "../constants.h"
#include <cassert>

using namespace std;

struct filter_state_s {
  float in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  float out1 = 0, out2 = 0, out3 = 0, out4 = 0;

  void apply(int numChannels, size_t blockSamples, int channel, float * buffer, float fcut, float fres, bool is_highpass) {
    for (size_t i = 0; i < blockSamples; i++) {
      size_t offset = numChannels * i + channel;
      float input = buffer[offset];
      float si = input;
      float f = fcut * 1.16;
      float ff = f * f;
      float fb = fres * (1.0 - 0.15 * ff);
      f = 1 - f;
      
      input -= out4 * fb;
      input *= 0.35013f * ff * ff;
      out1 = input + 0.3 * in1 + f * out1; // Pole 1
      in1  = input;
      out2 = out1 + 0.3 * in2 + f * out2;  // Pole 2
      in2 = out1;
      out3 = out2 + 0.3 * in3 + f * out3;  // Pole 3
      in3  = out2;
      out4 = out3 + 0.3 * in4 + f * out4;  // Pole 4
      in4  = out3;
      
      if (is_highpass) buffer[offset] = si - out4;
      else buffer[offset] = out4;
    }
  }
};

class FilterState : public TrackState {
public:
  FilterState(const ChannelConfiguration & channel_config, const Filter & filter, const Envelope & envelope, bool use_aftertouch)
    : TrackState(channel_config),
      fcut_min_(filter.get_fcut_min()),
      fcut_max_(filter.get_fcut_max()),
      fres_(filter.get_fres()),
      is_highpass_(filter.get_is_highpass()),
      envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true),
      use_aftertouch_(use_aftertouch)
  { }  
  
  SampleData render(int frames) override {
    auto input_data = TrackState::render(frames);
    
    if ((fcut_min_ < 1.0 && fcut_max_ < 1.0) || fres_ > 0.0) {    
      auto buffer = input_data.data();
      auto numSamples = input_data.size();
      auto numChannels = input_data.numberOfChannels();

      auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;
      
      while (numSamples) {
	size_t blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
	float current_fcut = fcut_min_ + envelope_state_.getLevel() * aftertouch_value * (fcut_max_ - fcut_min_);
	
	if (numChannels == 1) {
	  left_state_.apply(numChannels, blockSamples, 0, buffer, current_fcut, fres_, is_highpass_);
	} else {
	  left_state_.apply(numChannels, blockSamples, 0, buffer, current_fcut, fres_, is_highpass_);
	  right_state_.apply(numChannels, blockSamples, 1, buffer, current_fcut, fres_, is_highpass_);
	}
	
	buffer += numChannels * blockSamples;
	numSamples -= blockSamples;
	envelope_state_.process(blockSamples);
      }
    }

    return input_data;
  }

private:
  float fcut_min_, fcut_max_, fres_;
  bool is_highpass_, use_aftertouch_;

  filter_state_s left_state_, right_state_;

  EnvelopeState envelope_state_;
};

std::unique_ptr<TrackState>
Filter::createState(const ChannelConfiguration & config) const {
  return make_unique<FilterState>(config, *this, envelope_, use_aftertouch_);
}

void
Filter::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
  
  if (input.has("fcut")) {
    fcut_min_ = fcut_max_ = input.getFloat("fcut", 0.0f);
  } else {
    fcut_min_ = input.getFloat("fcutmin", 0.0f);
    fcut_max_ = input.getFloat("fcutmax", 0.0f);
  }

  fres_ = input.getFloat("fres");
  use_aftertouch_ = input.getBool("aftertouch");

  envelope_.loadParameters(input);
}

void
Filter::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  if (fcut_min_ == fcut_max_) {
    output.set("fcut", fcut_min_);
  } else {
    output.set("fcutmin", fcut_min_);
    output.set("fcutmax", fcut_max_);
  }

  output.set("fres", fres_);
  output.set("aftertouch", use_aftertouch_);

  envelope_.storeParameters(output);
}
