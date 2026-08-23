#include "Chorus.h"

#include "../dsp/ChorusEngine.h"
#include "../dsp/DelayLineTail.h"
#include "../audio/AudioBufferUtils.h"
#include "EffectTrackState.h"
#include "EffectVoiceState.h"

using namespace std;

namespace {

// Actual DSP, shared by ChorusTrackState and ChorusVoiceState.
//
// spatial_ (decided once, at construction, from channel_config - fixed
// for the object's whole lifetime, same as channel_config itself) picks
// between two entirely different internal shapes:
//  - Non-spatial (channel_config.isMono()): the original, pre-MonoEffect
//    behavior, unchanged - engine_ processes the single reduced Main
//    channel (plus Aux) in place, no width, no reencode
//    (reencodeIfNeeded() below is a pure passthrough in this mode,
//    matching every other MonoEffect's own isMono() early-out).
//  - Spatial: engine_ is built with 2 Main channels and decorrelate=true -
//    the same technique SoundFontVoice's own per-region chorus already
//    uses (SoundFont.cpp's chorus_engine_). applyEffect() duplicates the
//    reduced mono Main channel into both before calling engine_.process(),
//    so the two diverge into genuine stereo width purely from LFO phase
//    offset (a mono voice has no pre-existing width of its own to
//    preserve). Aux still travels through the very same process() call,
//    in its own always-present engine_ state, untouched by the Main
//    duplication. reencodeIfNeeded() below then spreads those two wet
//    Main channels as two point sources instead of collapsing to one.
class ChorusDsp {
public:
  ChorusDsp(const ChannelConfiguration & channel_config, int voices, float rate, float delay, float depth, float mix)
    : spatial_(!channel_config.isMono()),
      engine_(spatial_ ? 2 : reduceForEffect(channel_config).numberOfChannels(),
	      channel_config.getAudioOutSampleRate(), voices, rate, delay, depth,
	      /*decorrelate=*/spatial_)
  {
    engine_.setMix(mix);
  }

  // Non-spatial: mutates in place and hands the same buffer back,
  // unchanged shape - same as before this class supported spatial mode at
  // all. Spatial: returns a *new* buffer with 2 Main channels (the
  // decorrelated wet pair) instead of 1 - has to return rather than
  // mutate in place, since the channel count itself grows; see
  // ChorusTrackState/ChorusVoiceState's own render() for why this means
  // applyEffect() can't go through EffectTrackState/EffectVoiceState's
  // normal in-place virtual (same reason TapeDegradation's own
  // applyEffect() override is a no-op stub - see its class comment).
  AudioBuffer applyEffect(AudioBuffer data) {
    if (!spatial_) {
      engine_.process(data);
      return data;
    }

    bool has_main = data.hasChannel(Channel::Main);
    AudioBuffer wide(has_main ? 2 : 0, data.hasChannel(Channel::AuxA), data.hasChannel(Channel::AuxB), data.numberOfFrames());
    wide.zero();
    if (has_main) {
      auto src = data.getChannelData(0);
      auto c0 = wide.getChannelData(0), c1 = wide.getChannelData(1);
      for (int i = 0; i < data.numberOfFrames(); i++) c0[i] = c1[i] = src[i];
    }
    for (auto ch : { Channel::AuxA, Channel::AuxB }) {
      if (auto * src = data.getChannel(ch)) {
	auto dst = wide.getChannel(ch);
	for (int i = 0; i < data.numberOfFrames(); i++) dst[i] = src[i];
      }
    }
    engine_.process(wide);
    return wide;
  }

  // See ChorusEngine::getMaxDelaySamples()'s own doc comment - used by
  // ChorusVoiceState (below) to size its own DelayLineTail.
  int getMaxDelaySamples() const { return engine_.getMaxDelaySamples(); }

  // Non-spatial: `data` is already exactly the right shape (1 Main
  // channel matching channel_config's own MONO target) - pure
  // passthrough, same as every other MonoEffect's isMono() early-out.
  // Spatial: `data`'s 2 Main channels (applyEffect()'s own wet pair)
  // spread as two point sources around `position` -
  // encodeDecorrelatedPairAsSpreadPoint() (AmbisonicEncoding.h), the same
  // width-narrowed-by-distance technique SoundFontVoice's own per-region
  // chorus already uses. AuxA/AuxB carried straight through unencoded (a
  // shared-bus scalar has no direction), same as every other MonoEffect.
  AudioBuffer reencodeIfNeeded(const ChannelConfiguration & channel_config, const SphericalPosition & position, AudioBuffer data) {
    if (!spatial_) return data;

    bool has_main = data.hasChannel(Channel::Main);
    AudioBuffer out(has_main ? channel_config.numberOfChannels() : 0,
		    data.hasChannel(Channel::AuxA), data.hasChannel(Channel::AuxB), data.numberOfFrames());
    out.zero();
    if (has_main) {
      encodeDecorrelatedPairAsSpreadPoint(out, data.getChannelData(0), data.getChannelData(1), data.numberOfFrames(),
					   position, 15.0f, tap_encoders_[0], tap_encoders_[1]);
    }
    for (auto ch : { Channel::AuxA, Channel::AuxB }) {
      if (auto * src = data.getChannel(ch)) {
	auto dst = out.getChannel(ch);
	for (int i = 0; i < data.numberOfFrames(); i++) dst[i] = src[i];
      }
    }
    return out;
  }

private:
  bool spatial_;
  ChorusEngine engine_;
  array<AmbisonicVoiceEncoder, 2> tap_encoders_; // only meaningful when spatial_
};

class ChorusTrackState : public EffectTrackState {
public:
  ChorusTrackState(const ChannelConfiguration & channel_config, const SphericalPosition & position, int voices, float rate, float delay, float depth, float mix)
    : EffectTrackState(channel_config), position_(position), dsp_(channel_config, voices, rate, delay, depth, mix) { }

  AudioBuffer render(int frames, const InstrumentPool & instruments, RenderContext & context) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, instruments, context, reduced_config);
    auto wet = dsp_.applyEffect(std::move(data));
    setTrackInfo(TrackInfo( true, wet.isClipping() ));
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), position_, std::move(wet));
  }

protected:
  // Unused - render() above calls dsp_.applyEffect() directly (it needs
  // to return a possibly-widened buffer, which the in-place void contract
  // below can't express) - same as TapeDegradation's identically-shaped
  // stub.
  void applyEffect(AudioBuffer &) override { }

private:
  SphericalPosition position_;
  ChorusDsp dsp_;
};

// Voice-attached only (a track-attached ChorusTrackState is always
// rendered regardless of activity - see TrackState::renderChildren(),
// which never gates on isActive() the way VoiceState::renderChildren()
// does - so it never hits the bug tail_ exists for): without this, the
// instant the wrapped instrument's own children go inactive,
// renderChildren() reports zero Main channels, applyEffect() has nothing
// to write chorused output into even though ChorusEngine's own delay
// line still has real (pre-silence) content queued up, and this voice's
// isActive() (EffectVoiceState's default, children-only) already reads
// false - so InstrumentTrackState::renderVoices() simply stops calling
// render() on it at all, discarding that queued content outright. tail_
// (DelayLineTail.h) tracks how many more samples of real chorused output
// are still owed after the input goes silent; ensureMainChannel()
// (AudioBufferUtils.h) gives ChorusEngine a real buffer to keep
// writing/reading through for that long instead of skipping the block.
class ChorusVoiceState : public EffectVoiceState {
public:
  ChorusVoiceState(const ChannelConfiguration & channel_config, const SphericalPosition & position, int voices, float rate, float delay, float depth, float mix)
    : EffectVoiceState(channel_config), position_(position), dsp_(channel_config, voices, rate, delay, depth, mix), tail_(dsp_.getMaxDelaySamples()) { }

  AudioBuffer render(int frames) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto raw = renderChildren(frames, reduced_config);
    tail_.update(raw.hasChannel(Channel::Main), frames);
    auto data = ensureMainChannel(std::move(raw), frames);
    auto wet = dsp_.applyEffect(std::move(data));
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), position_, std::move(wet));
  }

  // A 2Lxx/2Rxx azimuth slide targeting the note this instance wraps
  // should still be audible through it - mirrors
  // TapeDegradationVoiceState::adjustAzimuth()'s identical reasoning.
  void adjustAzimuth(float delta) override {
    EffectVoiceState::adjustAzimuth(delta);
    position_.azimuth += delta;
  }

  // The actual fix: stays active - and so keeps being rendered - for as
  // long as the delay line might still have real pre-silence content
  // queued up, not just while children are. Mirrors
  // TapeDegradationVoiceState's identically-shaped override
  // (effects/TapeDegradation.cpp) and EnvelopeFilterVoiceState's own
  // "my own state, not just my children, decides isActive()" precedent
  // (effects/EnvelopeFilter.cpp).
  bool isActive() const override {
    return VoiceState::isActive() || tail_.isDraining();
  }

protected:
  void applyEffect(AudioBuffer &) override { } // see ChorusTrackState's own identical comment

private:
  SphericalPosition position_;
  ChorusDsp dsp_;
  DelayLineTail tail_;
};

}

std::unique_ptr<TrackState>
Chorus::createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const {
  return make_unique<ChorusTrackState>(channel_config, getPosition(), voices_, rate_, delay_, depth_, mix_);
}

std::unique_ptr<VoiceState>
Chorus::createVoiceState(const ChannelConfiguration & channel_config) const {
  // No position known through this path (see Track.h's own comment on
  // when it's actually reached) - the "no direction authored" sentinel,
  // same convention TapeDegradation::createVoiceState()/
  // Distortion::createVoiceState() use.
  return make_unique<ChorusVoiceState>(channel_config, SphericalPosition{}, voices_, rate_, delay_, depth_, mix_);
}

std::unique_ptr<VoiceState>
Chorus::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune,
		  float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  // Mirrors Track::playNote()'s own default body (Track.h) exactly, except
  // the group node it builds is a real ChorusVoiceState (carrying the
  // note's real position) rather than the generic createVoiceState()-built
  // wrapper the default uses - same shape as TapeDegradation::playNote()/
  // Distortion::playNote().
  auto group = make_unique<ChorusVoiceState>(config, position, voices_, rate_, delay_, depth_, mix_);
  auto child_config = getChildChannelConfiguration(config);
  for (auto & child : getChildren()) {
    auto voice = child->playNote(child_config, position, frequency, detune, velocity, note_value, sends, note_coord, needs_decorrelation);
    if (voice.get()) group->addChild(child->getInternalId(), std::move(voice));
  }
  return group;
}

void
Chorus::loadParameters(const ParameterSource & input) {
  MonoEffect::loadParameters(input);

  voices_ = input.get<int>("voices", 3);
  rate_ = input.get<float>("rate", 0.5f);
  delay_ = input.get<float>("delay", 15.0f);
  depth_ = input.get<float>("depth", 4.0f);
  mix_ = input.get<float>("mix", 0.5f);
}

void
Chorus::storeParameters(ParameterSource & output) const {
  MonoEffect::storeParameters(output);

  output.set("voices", voices_);
  output.set("rate", rate_);
  output.set("delay", delay_);
  output.set("depth", depth_);
  output.set("mix", mix_);
}
