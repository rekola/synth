#include "EnvelopeFilter.h"

#include "EffectState.h"
#include "../EnvelopeState.h"
#include "../constants.h"

using namespace std;

class EnvelopeFilterState : public EffectState {
public:
  EnvelopeFilterState(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : EffectState(channel_config), envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true) {
      
  }

  void applyEffect(SampleData & input_data) override {
    // A gain multiply is channel-count-agnostic by construction - applies
    // identically to however many channels are actually present (including
    // ambisonic ones), not just the first two.
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();

    size_t offset = 0;
    while (numSamples) {
      auto blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      auto gain = envelope_state_.getLevel();

      for (int c = 0; c < numChannels; c++) {
	auto buffer = input_data.getChannelData(c) + offset;
	for (decltype(blockSamples) i = 0; i < blockSamples; i++) {
	  buffer[i] *= gain;
	}
      }

      offset += blockSamples;
      numSamples -= blockSamples;
      envelope_state_.process(blockSamples);

      // Silence-kill threshold - see SoundFontVoice::render()'s identical
      // check (SoundFont.cpp) for the full reasoning. isReleasing() gates
      // this to the release stage only: a held note's gain can
      // legitimately be this quiet during ATTACK or a deliberately quiet
      // SUSTAIN and must never be killed early regardless. Unlike SF2
      // there's no separate static gain term to combine and no modenv_ to
      // keep in sync - `gain` (fetched above, before this block's decay)
      // is already the entire multiplicative factor being applied.
      // Jumping straight to DONE reuses the same reaping path isActive()
      // already relies on - and since isActive() (below) depends only on
      // this envelope, ignoring children entirely, freeing it here also
      // frees whatever child chain it wraps (e.g. a unison stack), which
      // otherwise keeps rendering at full cost for the whole release -
      // "let children play" (stopNote(), below) means nothing else ever
      // stops them early.
      if (envelope_state_.isReleasing() && gainToDecibels(gain) < constants::SILENCE_KILL_FLOOR_DB) {
	envelope_state_.nextSegment(EnvelopeState::RELEASE);
      }
    }
  }
  
  bool isActive() const override { return !envelope_state_.isDone(); }

  float getOwnLoudnessFactor() const override { return envelope_state_.getLevel(); }

  void stopNote() override {
    // let children play
    envelope_state_.nextSegment(EnvelopeState::SUSTAIN);
  }
  
  void killNote() override {
    TrackState::killNote(); // kill the children too
    envelope_state_.nextSegment(EnvelopeState::DONE);
  }

 protected:
  EnvelopeState envelope_state_;
};

std::unique_ptr<TrackState>
EnvelopeFilter::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<EnvelopeFilterState>(channel_config, envelope_);
}
