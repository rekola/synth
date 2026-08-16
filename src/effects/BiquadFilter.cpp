#include "BiquadFilter.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

#include "../dsp/Biquad.h"
#include "../EnvelopeState.h"

#include "../constants.h"

#include <array>
#include <cassert>
#include <vector>

using namespace std;

namespace {

// Actual DSP, shared by BiquadFilterTrackState and BiquadFilterVoiceState -
// see EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md. `aftertouch_value` is passed in
// already resolved by the caller rather than read here via getAftertouch()
// - that's a VoiceState-only concept (aftertouch never reaches a
// persistent track-tree node - see InstrumentTrackState::applyAftertouch()),
// so BiquadFilterTrackState always passes 1.0f and
// BiquadFilterVoiceState passes use_aftertouch_ ? getAftertouch() : 1.0f,
// keeping this helper itself independent of which role it's plugged into.
class BiquadFilterDsp {
public:
  BiquadFilterDsp(const ChannelConfiguration & channel_config, FilterType type, float fc, float Q, float peakGainDB, const Envelope & envelope)
    : envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true)
  {
    for (int c = 0; c < channel_config.numberOfChannels(); c++) {
      filters_.emplace_back(type, fc, Q, peakGainDB);
    }
    // Own persistent filter state for AuxA/AuxB, kept separate from
    // filters_ (Main-only, indexed 0..regularChannelCount()-1) rather than
    // just widening filters_ by 2 slots and indexing by raw channel
    // position - Main's regular-channel count can itself be 0 some blocks
    // (Send Main = 0 - see AudioBuffer.h), which would shift what a given
    // raw index *means* block to block and corrupt a filter's continuous
    // IIR history with another channel's. AuxA/AuxB's own slot here always
    // means the same thing regardless of Main's presence that block.
    aux_filters_[0] = Biquad<double>(type, fc, Q, peakGainDB);
    aux_filters_[1] = Biquad<double>(type, fc, Q, peakGainDB);
  }

  // Filters every channel - Main and AuxA/AuxB alike: the reverb/delay bus
  // should hear the same tonal shaping the dry signal does, the same
  // reasoning as Amplifier/EnvelopeFilter/Compressor/Tremolo/Distortion.
  // Returns whether there was anything to filter this block, for the
  // caller's own isEffectActive() bookkeeping.
  bool applyEffect(AudioBuffer & input_data, float aftertouch_value) {
    bool has_content = input_data.numberOfChannels() > 0;
    if (!has_content) return false;

    auto numSamples = input_data.size();
    int mainChannels = input_data.regularChannelCount();

    size_t offset = 0;
    while (numSamples) {
      int blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;

      // Same filter, applied identically & independently per channel -
      // including ambisonic ones (see AmbisonicEncoding.h): for a static
      // source position this is exactly equivalent to filtering the
      // pre-encode mono signal once, so direction is preserved exactly.
      // Whichever of Main/AuxA/AuxB happens to be absent this specific
      // block still gets its own filter's state advanced through silence
      // (Biquad::apply(blockSamples), no buffer) rather than skipped
      // outright - see that overload's own doc comment for why - but only
      // once that channel has actually had real data at least once
      // (main_ever_present_/aux_ever_present_): a channel that's never
      // existed yet has nothing to keep continuous, so there's no point
      // silence-feeding a filter that's still sitting at its untouched
      // initial state.
      (void) aftertouch_value; // reserved: no current filter parameter reads aftertouch here (see ResonantFilter/Tremolo for the pattern)
      if (mainChannels > 0) main_ever_present_ = true;
      for (size_t c = 0; c < filters_.size(); c++) {
	if (static_cast<int>(c) < mainChannels) {
	  filters_[c].apply(static_cast<size_t>(blockSamples), input_data.getChannelData(static_cast<int>(c)) + offset);
	} else if (main_ever_present_) {
	  filters_[c].apply(static_cast<size_t>(blockSamples));
	}
      }
      for (int a = 0; a < 2; a++) {
	auto * buf = input_data.getChannel(a == 0 ? Channel::AuxA : Channel::AuxB);
	if (buf) {
	  aux_ever_present_[static_cast<size_t>(a)] = true;
	  aux_filters_[static_cast<size_t>(a)].apply(static_cast<size_t>(blockSamples), buf + offset);
	} else if (aux_ever_present_[static_cast<size_t>(a)]) {
	  aux_filters_[static_cast<size_t>(a)].apply(static_cast<size_t>(blockSamples));
	}
      }

      offset += static_cast<size_t>(blockSamples);
      numSamples -= blockSamples;
      envelope_state_.process(blockSamples);
    }
    return true;
  }

private:
  std::vector<Biquad<double>> filters_;
  std::array<Biquad<double>, 2> aux_filters_ { Biquad<double>(FilterType::lowpass), Biquad<double>(FilterType::lowpass) };
  bool main_ever_present_ = false;
  std::array<bool, 2> aux_ever_present_ { false, false };
  EnvelopeState envelope_state_;
};

class BiquadFilterTrackState : public EffectTrackState {
public:
  BiquadFilterTrackState(const ChannelConfiguration & channel_config, FilterType type, float fc, float Q, float peakGainDB, const Envelope & envelope, bool use_aftertouch)
    : EffectTrackState(channel_config), dsp_(channel_config, type, fc, Q, peakGainDB, envelope) { (void) use_aftertouch; }

protected:
  // Aftertouch never reaches a persistent track-tree node (see
  // BiquadFilterDsp's own doc comment) - always neutral here regardless of
  // use_aftertouch_.
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input, 1.0f));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  BiquadFilterDsp dsp_;
};

class BiquadFilterVoiceState : public EffectVoiceState {
public:
  BiquadFilterVoiceState(const ChannelConfiguration & channel_config, FilterType type, float fc, float Q, float peakGainDB, const Envelope & envelope, bool use_aftertouch)
    : EffectVoiceState(channel_config), dsp_(channel_config, type, fc, Q, peakGainDB, envelope), use_aftertouch_(use_aftertouch) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    auto aftertouch_value = use_aftertouch_ ? getAftertouch() : 1.0f;
    setEffectActive(dsp_.applyEffect(input, aftertouch_value));
  }

private:
  BiquadFilterDsp dsp_;
  bool use_aftertouch_;
};

}

std::unique_ptr<TrackState>
BiquadFilter::createState(const ChannelConfiguration & config) const {
  return make_unique<BiquadFilterTrackState>(config, type_, fc_ / config.getAudioOutSampleRate(), Q_, peakGainDB_, envelope_, use_aftertouch_);
}

std::unique_ptr<VoiceState>
BiquadFilter::createVoiceState(const ChannelConfiguration & config) const {
  return make_unique<BiquadFilterVoiceState>(config, type_, fc_ / config.getAudioOutSampleRate(), Q_, peakGainDB_, envelope_, use_aftertouch_);
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
