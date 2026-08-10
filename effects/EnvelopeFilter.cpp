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

  void applyEffect(AudioBuffer & input_data) override {
    // A gain multiply is channel-count-agnostic by construction - applies
    // identically to however many channels are actually present (including
    // ambisonic ones), not just the first two.
    auto numSamples = input_data.size();
    auto numChannels = input_data.numberOfChannels();

    size_t offset = 0;
    while (numSamples) {
      auto blockSamples = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      auto gainStart = envelope_state_.getLevel();

      envelope_state_.process(blockSamples);

      // Silence-kill threshold - see SoundFontVoice::render()'s identical
      // check (SoundFont.cpp) for the full reasoning. isReleasing() gates
      // this to the release stage only: a held note's gain can
      // legitimately be this quiet during ATTACK or a deliberately quiet
      // SUSTAIN and must never be killed early regardless. Unlike SF2
      // there's no separate static gain term to combine and no modenv_ to
      // keep in sync - `gainStart` (fetched above, before this block's
      // decay) is already the entire multiplicative factor this block
      // starts at. Jumping straight to DONE reuses the same reaping path
      // isActive() already relies on - and since isActive() (below)
      // depends only on this envelope, ignoring children entirely,
      // freeing it here also frees whatever child chain it wraps (e.g. a
      // unison stack), which otherwise keeps rendering at full cost for
      // the whole release - "let children play" (stopNote(), below) means
      // nothing else ever stops them early.
      if (envelope_state_.isReleasing() && gainToDecibels(gainStart) < constants::SILENCE_KILL_FLOOR_DB) {
	envelope_state_.nextSegment(EnvelopeState::RELEASE);
      }

      // Interpolate this block's gain sample-by-sample (gainStart ramping
      // to the now-updated post-process() level) rather than holding
      // gainStart flat across the whole block. RENDER_EFFECTSAMPLEBLOCK
      // (64 samples) is coarse relative to a fast-released envelope's
      // ~10ms/441-sample exponential decay (~26% level drop per block) -
      // a flat per-block gain there is an audible staircase, not just an
      // inaudible quantization step (confirmed: this was the residual
      // click surviving EnvelopeFilterState::fastRelease()'s own fix,
      // traced to exact RENDER_EFFECTSAMPLEBLOCK boundaries during the
      // fast release). Slow, ordinary ADSR segments change little enough
      // per block that the linear ramp is indistinguishable from the true
      // (possibly exponential) curve.
      auto gainEnd = envelope_state_.getLevel();
      auto gainStep = blockSamples > 0 ? (gainEnd - gainStart) / static_cast<float>(blockSamples) : 0.0f;

      for (int c = 0; c < numChannels; c++) {
	auto buffer = input_data.getChannelData(c) + offset;
	float g = gainStart;
	for (decltype(blockSamples) i = 0; i < blockSamples; i++) {
	  buffer[i] *= g;
	  g += gainStep;
	}
      }

      offset += blockSamples;
      numSamples -= blockSamples;
    }
  }

  bool isActive() const override { return !envelope_state_.isDone(); }

  float getOwnLoudnessFactor() const override { return envelope_state_.getLevel(); }

  void stopNote() override {
    // let children play
    envelope_state_.nextSegment(EnvelopeState::SUSTAIN);
  }

  // Reclaims this voice quickly without a hard cut - see TrackState::
  // fastRelease()'s own comment for when this is used (identity-based
  // retrigger cutoff). Without this override, fastRelease() would fall
  // through to TrackState's default (recurse straight into children_),
  // bypassing envelope_state_ entirely and killing the wrapped oscillator/
  // sample instantly (InstrumentVoice::killNote() zeroes freq_ with no
  // ramp) - an abrupt amplitude jump, audible as a click on every
  // same-identity retrigger. Forcing release_ to 0 makes
  // EnvelopeState::nextSegment(SUSTAIN) fall back to TSF_FASTRELEASETIME
  // (10ms), same mechanism SoundFontVoice::fastRelease() already uses -
  // still "let children play", just a compressed fade instead of the
  // authored release time, never an instant cut.
  void fastRelease() override {
    envelope_state_.parameters.release_ = 0.0f;
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
