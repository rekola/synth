#include "Filter.h"

#include "SampleData.h"
#include "TrackState.h"
#include "EnvelopeState.h"

#include "defaults.h"
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
  FilterState(const ChannelConfiguration & channel_config, const Filter & filter, const Envelope & envelope)
    : TrackState(channel_config), fcut_min(filter.get_fcut_min()), fcut_max(filter.get_fcut_max()), fres(filter.get_fres()), is_highpass(filter.get_is_highpass()), envelope_state(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true) { }

  void applyAftertouch(float _aftertouch) override {
    TrackState::applyAftertouch(aftertouch);
    aftertouch = _aftertouch;
  }
  
  SampleData render(int frames) override {
    auto input_data = TrackState::render(frames);

    if ((fcut_min < 1.0 && fcut_max < 1.0) || fres > 0.0) {    
      auto buffer = input_data.data();
      auto numSamples = input_data.size();
      auto numChannels = input_data.numberOfChannels();
      
      while (numSamples) {
	size_t blockSamples = numSamples > RENDER_EFFECTSAMPLEBLOCK ? RENDER_EFFECTSAMPLEBLOCK : numSamples;
	float current_fcut = fcut_min + aftertouch * envelope_state.getLevel() * (fcut_max - fcut_min);
	
	if (numChannels == 1) {
	  left_state.apply(numChannels, blockSamples, 0, buffer, current_fcut, fres, is_highpass);
	} else {
	  left_state.apply(numChannels, blockSamples, 0, buffer, current_fcut, fres, is_highpass);
	  right_state.apply(numChannels, blockSamples, 1, buffer, current_fcut, fres, is_highpass);
	}
	
	buffer += numChannels * blockSamples;
	numSamples -= blockSamples;
	envelope_state.process(blockSamples);
      }
    }

    return input_data;
  }

private:
  float fcut_min, fcut_max, fres;
  float aftertouch = 1.0f;
  bool is_highpass;

  filter_state_s left_state, right_state;

  EnvelopeState envelope_state;
};

std::unique_ptr<TrackState>
Filter::createState(const ChannelConfiguration & config) const {
  return make_unique<FilterState>(config, *this, envelope_);
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
  aftertouch_ = input.getBool("aftertouch");

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
  output.set("aftertouch", aftertouch_);

  envelope_.storeParameters(output);
}
