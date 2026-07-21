#include "BiquadFilter.h"

#include "EffectState.h"

#include "../Biquad.h"
#include "../TrackState.h"
#include "../EnvelopeState.h"

#include "../constants.h"

#include <cassert>
#include <vector>

using namespace std;

class BiquadFilterState : public EffectState {
public:
  BiquadFilterState(const ChannelConfiguration & channel_config, FilterType type, float fc, float Q, float peakGainDB, const Envelope & envelope, bool use_aftertouch)
    : EffectState(channel_config),
      envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true),
      use_aftertouch_(use_aftertouch)
  {
    for (int c = 0; c < channel_config.numberOfChannels(); c++) {
      filters_.emplace_back(type, fc, Q, peakGainDB);
    }
  }

  void applyEffect(SampleData & input_data) override {
    if (!input_data.isZero() || isEffectActive()) {
      input_data.setNonZero();
      setEffectActive(true);

      auto numSamples = input_data.size();
      // Regular channels only - filters_ is sized once, at construction, from
      // the plain ChannelConfiguration (which stays unaware of SendA/SendB by
      // design, see SampleData.h). input_data.numberOfChannels() can be wider
      // whenever a send is present, so indexing filters_ with the raw count
      // would run past the end of that fixed-size vector - the send channels
      // are deliberately left untouched here rather than filtered, since they
      // need to reach the shared reverb/chorus bus unmodified.
      auto numChannels = input_data.numberOfChannels() - input_data.sendCount();
      auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;

      size_t offset = 0;
      while (numSamples) {
	int blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;

	// Same filter, applied identically & independently per channel -
	// including ambisonic ones (see AmbisonicEncoding.h): for a static
	// source position this is exactly equivalent to filtering the
	// pre-encode mono signal once, so direction is preserved exactly.
	for (int c = 0; c < numChannels; c++) {
	  filters_[static_cast<size_t>(c)].apply(static_cast<size_t>(blockSamples), input_data.getChannelData(c) + offset);
	}

	offset += static_cast<size_t>(blockSamples);
	numSamples -= blockSamples;
	envelope_state_.process(blockSamples);
      }
    }

    setTrackInfo(TrackInfo( isEffectActive(), input_data.isClipping()));
  }

private:
  bool use_aftertouch_;
  std::vector<Biquad<double>> filters_;
  EnvelopeState envelope_state_;
};

std::unique_ptr<TrackState>
BiquadFilter::createState(const ChannelConfiguration & config) const {
  return make_unique<BiquadFilterState>(config, type_, fc_ / config.getAudioOutSampleRate(), Q_, peakGainDB_, envelope_, use_aftertouch_);
}

void
BiquadFilter::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
  
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
  Effect::storeParameters(output);

  output.set("type", to_string(type_));
  output.set("fc", fc_);
  output.set("Q", Q_);
  output.set("aftertouch", use_aftertouch_);
  
  if (type_ == FilterType::peak || type_ == FilterType::lowshelf || type_ == FilterType::highshelf) {
    output.set("peakGainDB", peakGainDB_);
  }

  envelope_.storeParameters(output);
}
