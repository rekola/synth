#include "EnvelopeFilter.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"
#include "../state/EnvelopeState.h"
#include "../util/constants.h"

using namespace std;

namespace {

// Actual DSP, shared by EnvelopeFilterTrackState and
// EnvelopeFilterVoiceState - see EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md. The note-lifecycle forwarders
// below (stopNote()/fastRelease()/kill()) only matter for the voice role
// (nothing ever calls stopNote()/fastRelease()/killNote() on a
// persistent track-tree node - see InstrumentTrackState::stopVoices()/
// retriggerVoices()/chokeExclusiveClasses(), the only real callers) but
// live here anyway since they're just as cheap to share as the DSP
// itself; only EnvelopeFilterVoiceState actually calls them.
class EnvelopeFilterDsp {
public:
  EnvelopeFilterDsp(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : envelope_state_(channel_config.getAudioOutSampleRate(), envelope, 0, 0, true) { }

  void applyEffect(AudioBuffer & input_data) {
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
      if (envelope_state_.isReleasing() && TrackState::gainToDecibels(gainStart) < constants::SILENCE_KILL_FLOOR_DB) {
	envelope_state_.nextSegment(EnvelopeState::RELEASE);
      }

      // Interpolate this block's gain sample-by-sample (gainStart ramping
      // to the now-updated post-process() level) rather than holding
      // gainStart flat across the whole block. RENDER_EFFECTSAMPLEBLOCK
      // (64 samples) is coarse relative to a fast-released envelope's
      // ~10ms/441-sample exponential decay (~26% level drop per block) -
      // a flat per-block gain there is an audible staircase, not just an
      // inaudible quantization step (confirmed: this was the residual
      // click surviving EnvelopeFilterVoiceState::fastRelease()'s own fix,
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

      offset += static_cast<size_t>(blockSamples);
      numSamples -= blockSamples;
    }
  }

  bool isDone() const { return envelope_state_.isDone(); }
  float getLevel() const { return envelope_state_.getLevel(); }

  void stopNote() {
    // let children play
    envelope_state_.nextSegment(EnvelopeState::SUSTAIN);
  }

  // Reclaims this voice quickly without a hard cut - see VoiceState::
  // fastRelease()'s own comment for when this is used (identity-based
  // retrigger cutoff). Without this, fastRelease() would fall through to
  // VoiceState's default (recurse straight into children_), bypassing
  // envelope_state_ entirely and killing the wrapped oscillator/sample
  // instantly (InstrumentVoice::killNote() zeroes freq_ with no ramp) - an
  // abrupt amplitude jump, audible as a click on every same-identity
  // retrigger. Forcing release_ to 0 makes EnvelopeState::
  // nextSegment(SUSTAIN) fall back to TSF_FASTRELEASETIME (10ms), same
  // mechanism SoundFontVoice::fastRelease() already uses - still "let
  // children play", just a compressed fade instead of the authored
  // release time, never an instant cut.
  void fastRelease() {
    envelope_state_.parameters.release_ = 0.0f;
    envelope_state_.nextSegment(EnvelopeState::SUSTAIN);
  }

  void kill() { envelope_state_.nextSegment(EnvelopeState::DONE); }

private:
  EnvelopeState envelope_state_;
};

class EnvelopeFilterTrackState : public EffectTrackState {
public:
  EnvelopeFilterTrackState(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : EffectTrackState(channel_config), dsp_(channel_config, envelope) { }

  bool isActive() const override { return !dsp_.isDone(); }

protected:
  void applyEffect(AudioBuffer & input_data) override { dsp_.applyEffect(input_data); }

private:
  EnvelopeFilterDsp dsp_;
};

class EnvelopeFilterVoiceState : public EffectVoiceState {
public:
  EnvelopeFilterVoiceState(const ChannelConfiguration & channel_config, const Envelope & envelope)
    : EffectVoiceState(channel_config), dsp_(channel_config, envelope) { }

  bool isActive() const override { return !dsp_.isDone(); }

  float getOwnLoudnessFactor() const override { return dsp_.getLevel(); }

  void stopNote() override { dsp_.stopNote(); }
  void fastRelease() override { dsp_.fastRelease(); }

  void killNote() override {
    VoiceState::killNote(); // kill the children too
    dsp_.kill();
  }

protected:
  void applyEffect(AudioBuffer & input_data) override { dsp_.applyEffect(input_data); }

private:
  EnvelopeFilterDsp dsp_;
};

}

std::unique_ptr<TrackState>
EnvelopeFilter::createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const {
  return make_unique<EnvelopeFilterTrackState>(channel_config, envelope_);
}

std::unique_ptr<VoiceState>
EnvelopeFilter::createVoiceState(const ChannelConfiguration & channel_config) const {
  return make_unique<EnvelopeFilterVoiceState>(channel_config, envelope_);
}
