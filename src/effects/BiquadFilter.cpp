#include "BiquadFilter.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

#include "../dsp/Biquad.h"
#include "../dsp/ChannelBank.h"
#include "../state/EnvelopeState.h"

#include "../util/constants.h"

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
    : envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true),
      main_slots_(channel_config.numberOfChannels()),
      bank_(main_slots_ + 2, coefficientsFor(type, fc, Q, peakGainDB))
  { }

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
    if (mainChannels > main_slots_) mainChannels = main_slots_;

    // Same filter, applied identically & independently per channel -
    // including ambisonic ones (see AmbisonicEncoding.h): for a static
    // source position this is exactly equivalent to filtering the
    // pre-encode mono signal once, so direction is preserved exactly.
    // All channels advance together, one per vector lane. Main sits in
    // slots 0..main_slots_-1 and AuxA/AuxB in their own two slots after it,
    // rather than by raw buffer position - Main's regular-channel count can
    // itself be 0 some blocks (Send Main = 0 - see AudioBuffer.h), which
    // would shift what a given raw index *means* block to block and corrupt
    // a filter's continuous IIR history with another channel's. A slot with
    // no signal this block is fed silence, so its history keeps decaying
    // rather than freezing (and a slot that never had signal stays zero).
    (void) aftertouch_value; // reserved: no current filter parameter reads aftertouch here (see ResonantFilter/Tremolo for the pattern)
    float * main_planes[dsp::kMaxBankChannels] = {};
    for (int c = 0; c < mainChannels; c++) main_planes[c] = input_data.getChannelData(c);
    float * aux_planes[2] = { input_data.getChannel(Channel::AuxA), input_data.getChannel(Channel::AuxB) };

    size_t offset = 0;
    while (numSamples) {
      int blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;

      for (int c = 0; c < main_slots_; c++) bank_.plane(c) = main_planes[c] ? main_planes[c] + offset : nullptr;
      for (int a = 0; a < 2; a++) bank_.plane(main_slots_ + a) = aux_planes[a] ? aux_planes[a] + offset : nullptr;
      bank_.apply(blockSamples);

      offset += static_cast<size_t>(blockSamples);
      numSamples -= blockSamples;
      envelope_state_.process(blockSamples);
    }
    return true;
  }

private:
  static dsp::BiquadBank::Coefficients coefficientsFor(FilterType type, float fc, float Q, float peakGainDB) {
    auto c = Biquad<double>(type, fc, Q, peakGainDB).coefficients();
    return { c.a0, c.a1, c.a2, c.b1, c.b2 };
  }

  EnvelopeState envelope_state_;
  int main_slots_;
  dsp::BiquadBank bank_;
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
BiquadFilter::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  return make_unique<BiquadFilterTrackState>(config, type_, fc_ / config.getAudioOutSampleRate(), Q_, peakGainDB_, envelope_, use_aftertouch_);
}

std::unique_ptr<VoiceState>
BiquadFilter::createVoiceState(const ChannelConfiguration & config) const {
  return make_unique<BiquadFilterVoiceState>(config, type_, fc_ / config.getAudioOutSampleRate(), Q_, peakGainDB_, envelope_, use_aftertouch_);
}

void
BiquadFilter::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  auto type_text = input.get<std::string>("type");
  if (type_text == "lowpass") type_ = FilterType::lowpass;
  else if (type_text == "highpass") type_ = FilterType::highpass;
  else if (type_text == "bandpass") type_ = FilterType::bandpass;
  else if (type_text == "notch") type_ = FilterType::notch;
  else if (type_text == "peak") type_ = FilterType::peak;
  else if (type_text == "lowshelf") type_ = FilterType::lowshelf;
  else if (type_text == "highshelf") type_ = FilterType::highshelf;
  else type_ = FilterType::lowpass;

  fc_ = input.get<float>("fc");
  Q_ = input.get<float>("Q");
  peakGainDB_ = input.get<float>("peakGainDB");
  use_aftertouch_ = input.get<bool>("aftertouch");

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
