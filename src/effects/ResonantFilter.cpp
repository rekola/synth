#include "ResonantFilter.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

#include "../dsp/MoogVCF.h"
#include "../state/EnvelopeState.h"
#include "../util/constants.h"

#include <array>
#include <cassert>
#include <vector>

using namespace std;

namespace {

// Actual DSP, shared by ResonantFilterTrackState and
// ResonantFilterVoiceState - see EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md. `aftertouch_value` is passed in
// already resolved by the caller rather than read here via getAftertouch()
// - that's a VoiceState-only concept (aftertouch never reaches a
// persistent track-tree node - see InstrumentTrackState::applyAftertouch()),
// so ResonantFilterTrackState always passes 1.0f and
// ResonantFilterVoiceState passes use_aftertouch_ ? getAftertouch() : 1.0f,
// keeping this helper itself independent of which role it's plugged into.
class ResonantFilterDsp {
public:
  ResonantFilterDsp(const ChannelConfiguration & channel_config, const ResonantFilter & filter, const Envelope & envelope)
    : cut_min_(filter.get_cut_min()),
      cut_max_(filter.get_cut_max()),
      res_(filter.get_res()),
      sample_rate_(static_cast<float>(channel_config.getAudioOutSampleRate())),
      envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true),
      filters_(static_cast<size_t>(channel_config.numberOfChannels()))
  { }

  // Filters every channel - Main and AuxA/AuxB alike: the reverb/delay bus
  // should hear the same tonal shaping the dry signal does, the same
  // reasoning as Amplifier/EnvelopeFilter/Compressor/Tremolo/Distortion/
  // BiquadFilter. AuxA/AuxB get their own persistent filter state
  // (aux_filters_), kept separate from filters_ (Main-only, indexed
  // 0..regularChannelCount()-1) - see BiquadFilter.cpp's own comment on
  // this for why (Main's regular-channel count can itself be 0 some
  // blocks, which would otherwise shift what a raw index means block to
  // block). Returns whether there was anything to filter this block, for
  // the caller's own isEffectActive() bookkeeping.
  bool applyEffect(AudioBuffer & input_data, float aftertouch_value) {
    auto numSamples = input_data.size();
    int mainChannels = input_data.regularChannelCount();

    bool has_content = input_data.numberOfChannels() > 0;

    size_t offset = 0;
    while (numSamples) {
      auto blockSamples = static_cast<size_t>(numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples);
      float current_cut = (cut_min_ + envelope_state_.getLevel() * aftertouch_value * (cut_max_ - cut_min_)) / (sample_rate_ * 0.5f);

      if (has_content) {
	// Same filter, applied identically & independently per channel -
	// including ambisonic ones (see AmbisonicEncoding.h): for a static
	// source position this is exactly equivalent to filtering the
	// pre-encode mono signal once, so direction is preserved exactly.
	// Whichever of Main/AuxA/AuxB happens to be absent this specific
	// block still gets its own filter's state advanced through silence
	// (MoogVCF::apply(blockSamples, fc, res), no buffer) rather than
	// skipped outright - but only once that channel has actually had
	// real data at least once (main_ever_present_/aux_ever_present_),
	// same reasoning as BiquadFilter.cpp.
	if (mainChannels > 0) main_ever_present_ = true;
	for (int c = 0; c < static_cast<int>(filters_.size()); c++) {
	  if (c < mainChannels) {
	    filters_[static_cast<size_t>(c)].apply(blockSamples, input_data.getChannelData(c) + offset, current_cut, res_);
	  } else if (main_ever_present_) {
	    filters_[static_cast<size_t>(c)].apply(blockSamples, current_cut, res_);
	  }
	}
	for (int a = 0; a < 2; a++) {
	  auto * buf = input_data.getChannel(a == 0 ? Channel::AuxA : Channel::AuxB);
	  if (buf) {
	    aux_ever_present_[static_cast<size_t>(a)] = true;
	    aux_filters_[static_cast<size_t>(a)].apply(blockSamples, buf + offset, current_cut, res_);
	  } else if (aux_ever_present_[static_cast<size_t>(a)]) {
	    aux_filters_[static_cast<size_t>(a)].apply(blockSamples, current_cut, res_);
	  }
	}
      }

      offset += blockSamples;
      numSamples -= static_cast<int>(blockSamples);
      envelope_state_.process(static_cast<int>(blockSamples));
    }

    return has_content;
  }

private:
  float cut_min_, cut_max_, res_;
  float sample_rate_;

  EnvelopeState envelope_state_;

  std::vector<MoogVCF<float>> filters_;
  std::array<MoogVCF<float>, 2> aux_filters_;
  bool main_ever_present_ = false;
  std::array<bool, 2> aux_ever_present_ { false, false };
};

class ResonantFilterTrackState : public EffectTrackState {
public:
  ResonantFilterTrackState(const ChannelConfiguration & channel_config, const ResonantFilter & filter, const Envelope & envelope, bool use_aftertouch)
    : EffectTrackState(channel_config), dsp_(channel_config, filter, envelope) { (void) use_aftertouch; }

protected:
  // Aftertouch never reaches a persistent track-tree node (see
  // ResonantFilterDsp's own doc comment) - always neutral here regardless
  // of use_aftertouch_.
  void applyEffect(AudioBuffer & input_data) override {
    setEffectActive(dsp_.applyEffect(input_data, 1.0f));
    setTrackInfo(TrackInfo( isEffectActive(), input_data.isClipping() ));
  }

private:
  ResonantFilterDsp dsp_;
};

class ResonantFilterVoiceState : public EffectVoiceState {
public:
  ResonantFilterVoiceState(const ChannelConfiguration & channel_config, const ResonantFilter & filter, const Envelope & envelope, bool use_aftertouch)
    : EffectVoiceState(channel_config), dsp_(channel_config, filter, envelope), use_aftertouch_(use_aftertouch) { }

protected:
  void applyEffect(AudioBuffer & input_data) override {
    auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;
    setEffectActive(dsp_.applyEffect(input_data, aftertouch_value));
  }

private:
  ResonantFilterDsp dsp_;
  bool use_aftertouch_;
};

}

std::unique_ptr<TrackState>
ResonantFilter::createState(const ChannelConfiguration & config) const {
  return make_unique<ResonantFilterTrackState>(config, *this, envelope_, use_aftertouch_);
}

std::unique_ptr<VoiceState>
ResonantFilter::createVoiceState(const ChannelConfiguration & config) const {
  return make_unique<ResonantFilterVoiceState>(config, *this, envelope_, use_aftertouch_);
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
