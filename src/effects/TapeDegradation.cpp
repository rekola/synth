#include "TapeDegradation.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"
#include "../dsp/FractionalDelayLine.h"
#include "../dsp/DelayLineTail.h"
#include "../dsp/Biquad.h"
#include "../dsp/FilterType.h"
#include "../dsp/HashField.h"
#include "../audio/AudioBufferUtils.h"
#include "TapeDegradationPresets.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {

// Self-contained (not TreeNode::decibelsToGain(), which only free-standing
// TreeNode<Derived> subclasses can reach) - same convention
// dsp/TapeTransport.cpp's own local dbToLinear() already uses.
inline float dbToLinear(float db) { return powf(10.0f, db * 0.05f); }

// Fixed compile-time salt, not derived from any per-instance state - see
// InstrumentVoice.h's own kNotePhaseSalt for the identical reasoning. The
// coordinate passed to seedFromCoord() below carries the per-instance
// variation instead: a track-attached instance's own track identity for
// TapeDegradationTrackState (no note to key off - see
// TapeDegradation::createState()), or the real note coordinate for
// TapeDegradationVoiceState.
constexpr uint64_t kTapeSeedSalt = 0x7A9E1D4C6F82B350ull;

// One-time-per-instance seed draw, shared by TapeDegradationTrackState and
// TapeDegradationVoiceState below - deterministic and reproducible given
// the same coordinate, safe to call from the audio thread (HashField has
// no shared state at all).
inline uint32_t seedFromCoord(const NoteCoordinate & note_coord) {
  return static_cast<uint32_t>(HashField(kTapeSeedSalt).unit(note_coord.toHashCoord(), paramId("tape_seed")) * 4294967295.0f);
}

// Standard exponential envelope-follower coefficient: fraction of the way
// from the current value to the target reached in one sample, given a
// time constant in milliseconds. timeMs <= 0 means "instantaneous"
// (coefficient 1 - track the input exactly, no smoothing).
inline float envelopeCoeff(float timeMs, float sampleRate) {
  if (timeMs <= 0.0f) return 1.0f;
  return 1.0f - expf(-1.0f / (sampleRate * (timeMs * 0.001f)));
}

// Zero slope at both ends - used for every spin-up/spin-down fade/droop
// curve below so a state transition (Running -> SpinDown, SpinDown ->
// Stopped) never introduces a discontinuity in the curve's own rate of
// change, only in which curve is driving it.
inline float smoothstep01(float x) {
  x = std::max(0.0f, std::min(1.0f, x));
  return x * x * (3.0f - 2.0f * x);
}

// Buffer sizing for the wow/flutter delay line - generous headroom around
// a fixed center delay, comfortably covering the parameter ranges in
// TapeDegradation.h (worst case: high depth + low rate wandering the
// delay-position integral further before its next zero-crossing - see
// applyEffect()'s own comment on why this is an integral, not a directly-
// authored sample offset). Not exposed as a parameter - purely internal
// plumbing, same as ChorusEngine's own bufLen sizing in its constructor.
constexpr float kCenterDelaySeconds = 0.010f;
constexpr float kMaxDelaySeconds = 0.060f;

// Shared by the delay line's own resize() and DelayLineTail's sizing (see
// TapeDegradationDsp's constructor) - both need to agree on exactly the
// same buffer length.
inline int wowDelayBufferSamples(int sampleRate) {
  return static_cast<int>(static_cast<float>(sampleRate) * kMaxDelaySeconds) + 8;
}

// A forced ("fast") note-off (killNote()/fastRelease() - see
// TapeDegradationVoiceState below) still needs *some* spin-down (the
// delay line still has to drain regardless - see DelayLineTail), but
// should never hold a voice open for the full musical spin-down when
// something urgently needs it reclaimed (retrigger, SF2 exclusive-class
// choke) - mirrors EnvelopeFilterDsp::fastRelease()'s own compressed
// 10ms release for the identical reason, just a hair longer since this
// one also carries an audible (if brief) fade rather than a bare gain
// ramp.
constexpr float kFastSpinDownSeconds = 0.02f;

// Actual DSP, shared by TapeDegradationTrackState and
// TapeDegradationVoiceState - see EffectTrackState.h/EffectVoiceState.h
// for why every concrete effect needs both a track-tree and a voice-chain
// state class sharing one non-polymorphic DSP implementation like this.
class TapeDegradationDsp {
public:
  TapeDegradationDsp(int sampleRate, uint32_t seed, const TapeTransportParams & params,
                      float saturationDriveDB, float lowCutHz, float hfRolloffHz, float headBumpHz, float headBumpGainDB, float mix,
                      float spinUpMs, float spinUpDepthCents, float spinDownMs, float droopDepthCents)
    : sampleRate_(sampleRate), transport_(seed), params_(params),
      saturation_drive_(dbToLinear(saturationDriveDB)),
      hf_rolloff_base_hz_(hfRolloffHz), mix_(mix),
      spin_up_seconds_(spinUpMs * 0.001f), spin_up_depth_cents_(spinUpDepthCents),
      spin_down_seconds_(spinDownMs * 0.001f), droop_depth_cents_(droopDepthCents),
      delay_tail_(wowDelayBufferSamples(sampleRate) - 4)
  {
    // Narrow-band low end (Cassette/Mellotron/Dictaphone; a plain "tape
    // player" leaves this at a near-inaudible default - see
    // TapeDegradationPresets.h) - a highpass at fc -> 0 already behaves as
    // a harmless near-bypass (only literal DC is affected), so no separate
    // on/off flag is needed here, same "process unconditionally"
    // convention every other per-track effect already uses.
    float fc_low = std::min(std::max(lowCutHz, 1.0f) / static_cast<float>(sampleRate_), 0.499f);
    float fc_hf = std::min(hfRolloffHz / static_cast<float>(sampleRate_), 0.499f); // Q ~ Butterworth-flat, matching InstrumentVoice.h's own floor-absorption filter convention
    float fc_bump = std::min(headBumpHz / static_cast<float>(sampleRate_), 0.499f);

    center_delay_samples_ = static_cast<float>(sampleRate_) * kCenterDelaySeconds;
    int buffer_samples = wowDelayBufferSamples(sampleRate_);
    delay_samples_ = center_delay_samples_;
    max_delay_samples_ = static_cast<float>(buffer_samples) - 4.0f; // readCubic()'s +2 margin on each side

    // Main and AuxA/AuxB all get identically-configured channel state
    // (same delay-line buffer length, same filter coefficients) - see
    // applyEffect()'s own comment on why this has to be one independent
    // instance per channel sharing one modulation source, not one shared
    // instance reused positionally.
    for (auto * ch : { &main_, &auxA_, &auxB_ }) {
      ch->delay_line.resize(buffer_samples);
      ch->low_cut.set(fc_low, 0.707f);
      ch->hf_rolloff.set(fc_hf, 0.707f);
      ch->head_bump.set(fc_bump, 0.9f, headBumpGainDB);
    }

    envelope_attack_coeff_ = envelopeCoeff(params.breathingAttackMs, static_cast<float>(sampleRate_));
    envelope_release_coeff_ = envelopeCoeff(params.breathingReleaseMs, static_cast<float>(sampleRate_));
  }

  // Note-lifecycle - see dsp/TapeTransport.h's own TapeTransportLifecycle
  // doc comment. Only ever called by TapeDegradationVoiceState (below) -
  // TapeDegradationTrackState never calls any of these, which is exactly
  // what keeps a track-attached instance running forever with no special
  // casing anywhere else (TapeTransportLifecycle defaults to, and stays
  // in, Running unless told otherwise).
  void noteOn() { lifecycle_.noteOn(spin_up_seconds_); }
  void noteOff() { lifecycle_.noteOff(spin_down_seconds_); }
  void noteOffFast() { lifecycle_.noteOff(kFastSpinDownSeconds); }

  // Worst-case samples this instance could still need to render *from the
  // moment noteOff() lands* before it's safe to reclaim the voice:
  // spin-down's own duration (it's actively generating audible fade/droop
  // content, not just idling) + the delay line's own maximum content age
  // (the last sample spin-down ever writes still needs that long to
  // propagate out through the wow/flutter delay line) + the configured
  // maximum dropout duration (an in-flight dip finishing out). A static
  // upper bound from configured parameters, not a live countdown - see
  // isSilent() for the actual dynamic check.
  int tailSamples() const {
    int spin_down_samples = static_cast<int>(spin_down_seconds_ * static_cast<float>(sampleRate_));
    int dropout_samples = static_cast<int>(params_.dropoutDurationMs * 0.001f * static_cast<float>(sampleRate_));
    return spin_down_samples + delay_tail_.maxDelaySamples() + dropout_samples;
  }

  // The real, dynamic "are we there yet" check TapeDegradationVoiceState's
  // own isActive() override relies on - true only once the lifecycle has
  // fully wound down *and* the wow/flutter delay line has had enough
  // additional silent samples to guarantee nothing pre-noteOff is still
  // queued up in it.
  bool isSilent() const {
    return lifecycle_.state() == TapeTransportLifecycle::State::Stopped && !delay_tail_.isDraining();
  }

  // Mono in place - `data` must already be single-channel (children were
  // constructed via reduceForEffect()) and have a real Main channel (see
  // AudioBufferUtils.h's ensureMainChannel() - both call sites below force
  // this before calling in). `sourceHadRealAudio`: whether this block's
  // input was genuine (not just the silence ensureMainChannel() stands in
  // for) - feeds delay_tail_'s own drain countdown, independent of
  // whatever the lifecycle state machine is doing.
  void applyEffect(AudioBuffer & data, bool sourceHadRealAudio) {
    delay_tail_.update(sourceHadRealAudio, data.numberOfFrames());
    if (data.numberOfChannels() == 0) return; // defensive; callers always ensureMainChannel() first

    using State = TapeTransportLifecycle::State;

    auto buffer = data.getChannelData(0);
    for (int i = 0; i < data.numberOfFrames(); i++) {
      auto ts = transport_.nextSample(params_, static_cast<float>(sampleRate_));
      lifecycle_.advance(1.0f / static_cast<float>(sampleRate_));
      float dry = buffer[i];

      auto state = lifecycle_.state();
      auto progress = lifecycle_.progress();

      // Envelope follower over the actual input - feeds Cassette's
      // Dolby-style HF breathing and Optical film's level-dependent grain
      // noise below, both inert (breathingAmount/hissLevelDependent = 0)
      // for every preset that doesn't explicitly use them, in which case
      // this is a cheap no-op tracked for nothing.
      float abs_dry = fabsf(dry);
      float env_coeff = abs_dry > envelope_ ? envelope_attack_coeff_ : envelope_release_coeff_;
      envelope_ += (abs_dry - envelope_) * env_coeff;
      float envelope_norm = std::min(envelope_, 2.0f);

      // Pitch: normal wow+flutter, plus whichever of spin-up's swoop-in or
      // spin-down's droop-out is currently active - see
      // dsp/TapeTransport.h's own doc comment on why this shaping lives
      // here rather than inside TapeTransportDsp itself. During SpinDown
      // the random wow/flutter jitter itself also fades toward 0 (a
      // slowing motor's speed variance shrinks along with its speed), so
      // the deliberate droop is what's left dominating by the time
      // Stopped is reached; Stopped itself applies no further pitch
      // modulation at all - delay_samples_ simply holds wherever it last
      // settled, avoiding a discontinuity from snapping back to 0.
      float wow_flutter_cents = ts.pitchDeviationCents;
      float lifecycle_pitch_cents = 0.0f;
      if (state == State::SpinUp) {
        lifecycle_pitch_cents = spin_up_depth_cents_ * (1.0f - smoothstep01(progress));
      } else if (state == State::SpinDown) {
        wow_flutter_cents *= (1.0f - smoothstep01(progress));
        lifecycle_pitch_cents = -droop_depth_cents_ * smoothstep01(progress);
      } else if (state == State::Stopped) {
        wow_flutter_cents = 0.0f;
      }
      float pitch_cents = wow_flutter_cents + lifecycle_pitch_cents;

      // Wow/flutter: a delay-line pitch shift comes from the delay's own
      // *rate of change*, not its absolute value, so the transport's
      // instantaneous pitch-deviation-in-cents has to be integrated into
      // a running delay position rather than read as a literal offset -
      // this is the tape-position-vs-write-position integral a physical
      // transport's own speed wobble corresponds to. Frozen during
      // Stopped (see above) - dry is always 0 by then anyway in every
      // realistic case, but freezing rather than continuing to integrate
      // 0-cents-deviation keeps delay_samples_ from drifting toward its
      // clamp bounds for no reason over an arbitrarily long Stopped tail.
      if (state != State::Stopped) {
        float speed_ratio = powf(2.0f, pitch_cents * (1.0f / 1200.0f));
        delay_samples_ += (1.0f - speed_ratio);
        delay_samples_ = std::max(2.0f, std::min(max_delay_samples_, delay_samples_));
      }

      // Lifecycle fade: 1 during SpinUp/Running, ramping to 0 across
      // SpinDown, pinned at 0 during Stopped - scales every component the
      // tape *generates itself* (hiss/rumble/click/dropout/amplitude
      // flutter), so the machine's own noise floor fades out smoothly
      // rather than cutting the instant Stopped begins. Never applied to
      // `dry`/`wobbled` - the note's own real content decays on the
      // instrument's own terms (its envelope), not this effect's.
      float lifecycle_fade = 1.0f;
      if (state == State::SpinDown) lifecycle_fade = 1.0f - smoothstep01(progress);
      else if (state == State::Stopped) lifecycle_fade = 0.0f;

      // Hiss (level-dependent grain, Optical film - inert unless
      // hissLevelDependent is set) + rumble (Vinyl) + click: additive, a
      // physical machine's own noise sits underneath (and, for a click,
      // briefly on top of) the program material, not multiplied into it.
      float additive = ts.hiss * (1.0f + params_.hissLevelDependent * envelope_norm) * lifecycle_fade
                      + ts.rumble * lifecycle_fade
                      + ts.clickImpulse * lifecycle_fade;

      // Dropout + pressure-pad amplitude flutter (Mellotron - inert,
      // ts.ampFlutter == 1, unless ampFlutterDepth is set): both are
      // uniform scalar gain multipliers, so they combine into one
      // multiply; both also fade toward "no effect" (gain 1) via the same
      // lifecycle_fade, same reasoning as the additive noise above.
      float dropout_gain = 1.0f - (1.0f - ts.dropoutGain) * lifecycle_fade;
      float amp_flutter = 1.0f - (1.0f - ts.ampFlutter) * lifecycle_fade;

      // Dolby-style HF breathing (Cassette - inert unless breathingAmount
      // is set): the rolloff's own cutoff tracks the envelope follower
      // above (Main-only detection - see its own comment), recomputed on
      // all three channels' own hf_rolloff instances every sample only
      // when actually in use, so every other preset pays nothing for this.
      if (params_.breathingAmount != 0.0f) {
        float mod = 1.0f + params_.breathingAmount * (envelope_norm - 0.5f);
        float fc_hz = std::max(50.0f, hf_rolloff_base_hz_ * mod);
        float fc_norm = std::min(fc_hz / static_cast<float>(sampleRate_), 0.499f);
        main_.hf_rolloff.setFc(fc_norm);
        auxA_.hf_rolloff.setFc(fc_norm);
        auxB_.hf_rolloff.setFc(fc_norm);
      }

      float drive = saturation_drive_ * (1.0f + (1.0f - ts.health) * params_.healthSensitivity);

      // Main and AuxA/AuxB (whichever are actually present this block) all
      // go through the same wow/flutter -> noise -> dropout -> tone ->
      // saturation chain, driven by the shared values just computed above
      // (delay_samples_, additive, dropout_gain, amp_flutter, drive) - so
      // a send hears the same tape machine the dry signal does, not a
      // bypassed clean copy (the same reasoning Compressor/EnvelopeFilter/
      // Tremolo/BiquadFilter already apply to Main and Aux alike). Each
      // channel still gets its *own* delay-line/filter state (ChannelState)
      // rather than one shared instance, since Main and Aux carry
      // differently-scaled copies of the same dry signal (InstrumentVoice::
      // encodePosition()'s own sends.a/sends.b) - only the modulation
      // driving them is shared, not their content. Aux state is still
      // advanced (write 0) even on a block where that channel is
      // momentarily absent, the same "advance through silence rather than
      // freeze" convention ChorusEngine's own aux handling already uses,
      // so it resumes correctly rather than as if no time had passed;
      // only Main is guaranteed present (ensureMainChannel()).
      buffer[i] = processChannel(main_, dry, additive, dropout_gain, amp_flutter, drive);

      auto * bufferA = data.getChannel(Channel::AuxA);
      float outA = processChannel(auxA_, bufferA ? bufferA[i] : 0.0f, additive, dropout_gain, amp_flutter, drive);
      if (bufferA) bufferA[i] = outA;

      auto * bufferB = data.getChannel(Channel::AuxB);
      float outB = processChannel(auxB_, bufferB ? bufferB[i] : 0.0f, additive, dropout_gain, amp_flutter, drive);
      if (bufferB) bufferB[i] = outB;
    }
  }

  // Parent only wants MONO (nested under another mono-reducing effect, or
  // the track itself is a MONO config) - nothing to point-encode, hand
  // back what's already there. Same early-out Chorus/Distortion's own
  // reencodeIfNeeded() has; the substantive difference from theirs is the
  // has_main branch below, which uses a real position instead of
  // encodeMonoAsPoint()'s omnidirectional fallback. AuxA/AuxB are carried
  // straight through unencoded here (a shared-bus scalar has no direction
  // to re-encode), same as Chorus/Distortion - but by this point they've
  // already been through applyEffect()'s own degradation, same as Main,
  // not a bypassed clean copy of what the child originally sent.
  AudioBuffer reencodeIfNeeded(const ChannelConfiguration & channel_config, const SphericalPosition & position, AudioBuffer data) {
    if (channel_config.isMono()) return data;

    bool has_main = data.hasChannel(Channel::Main);
    AudioBuffer out(has_main ? channel_config.numberOfChannels() : 0,
                     data.hasChannel(Channel::AuxA), data.hasChannel(Channel::AuxB), data.numberOfFrames());
    out.zero();
    if (has_main) {
      auto gains = computeAmbisonicGains(position);
      encoder_.encodeBlock(out, data.getChannelData(0), data.numberOfFrames(), gains);
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
  // One independent delay-line/filter instance per channel identity
  // (Main, AuxA, AuxB) - see applyEffect()'s own comment on why these
  // can't be shared even though the modulation driving them is. Default-
  // constructed with the right FilterType per member (Biquad<float> has
  // no default constructor) and reconfigured in the outer constructor's
  // body once the real cutoffs/Q are known.
  struct ChannelState {
    FractionalDelayLine delay_line;
    Biquad<float> low_cut { FilterType::highpass };
    Biquad<float> hf_rolloff { FilterType::lowpass };
    Biquad<float> head_bump { FilterType::peak };
  };

  // One sample's worth of the shared wow/flutter -> noise -> dropout ->
  // tone -> saturation chain for a single channel's own delay line/filter
  // state, given this sample's shared modulation values (delay_samples_,
  // additive noise, dropout/amp-flutter gain, saturation drive - all
  // computed once in applyEffect()'s own loop and passed in identically
  // for Main/AuxA/AuxB). Always runs, even when the caller's own buffer
  // for this channel is momentarily absent (dry = 0 passed in) - see
  // applyEffect()'s own "advance through silence" comment.
  float processChannel(ChannelState & ch, float dry, float additive, float dropout_gain, float amp_flutter, float drive) {
    ch.delay_line.write(dry);
    float wobbled = ch.delay_line.readCubic(delay_samples_);
    float with_noise = wobbled + additive;
    float dipped = with_noise * dropout_gain * amp_flutter;
    float toned = ch.head_bump.process(ch.hf_rolloff.process(ch.low_cut.process(dipped)));
    float saturated = tanhf(drive * toned);
    return mix_ * saturated + (1.0f - mix_) * dry;
  }

  int sampleRate_;
  TapeTransportDsp transport_;
  TapeTransportParams params_;

  float saturation_drive_;
  float hf_rolloff_base_hz_;
  float mix_;

  float envelope_ = 0.0f;
  float envelope_attack_coeff_ = 1.0f;
  float envelope_release_coeff_ = 1.0f;

  ChannelState main_, auxA_, auxB_;
  float center_delay_samples_ = 0.0f;
  float delay_samples_ = 0.0f;
  float max_delay_samples_ = 0.0f;

  TapeTransportLifecycle lifecycle_;
  float spin_up_seconds_;
  float spin_up_depth_cents_;
  float spin_down_seconds_;
  float droop_depth_cents_;
  DelayLineTail delay_tail_;

  AmbisonicVoiceEncoder encoder_;
};

class TapeDegradationTrackState : public EffectTrackState {
public:
  // note_coord: the track's own identity (TapeDegradation::createState()
  // passes getInternalId() - see NoteCoordinate.h's own doc comment on
  // why this is safe to use as a coordinate component within one run) -
  // a track-attached instance has no note to key off, unlike the
  // voice-attached one below.
  TapeDegradationTrackState(const ChannelConfiguration & channel_config, const SphericalPosition & position,
                             const TapeTransportParams & transport_params,
                             float saturationDriveDB, float lowCutHz, float hfRolloffHz, float headBumpHz, float headBumpGainDB, float mix,
                             float spinUpMs, float spinUpDepthCents, float spinDownMs, float droopDepthCents,
                             const NoteCoordinate & note_coord = {})
    : EffectTrackState(channel_config), position_(position),
      dsp_(channel_config.getAudioOutSampleRate(), seedFromCoord(note_coord), transport_params,
           saturationDriveDB, lowCutHz, hfRolloffHz, headBumpHz, headBumpGainDB, mix,
           spinUpMs, spinUpDepthCents, spinDownMs, droopDepthCents) { }
  // No noteOn()/noteOff() ever called here - a track-attached instance's
  // TapeTransportLifecycle simply stays in its default Running state for
  // its whole (song-length) lifetime, so it always behaves exactly like
  // the pre-lifecycle code: fully engaged, no spin-up/spin-down, never
  // reclaimed. See dsp/TapeTransport.h's own doc comment on
  // TapeTransportLifecycle.

  AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto raw = renderChildren(frames, instruments, context, reduced_config);
    bool had_real_audio = raw.hasChannel(Channel::Main);
    auto data = ensureMainChannel(std::move(raw), frames);
    dsp_.applyEffect(data, had_real_audio);
    setEffectActive(data.numberOfChannels() > 0);
    setTrackInfo(TrackInfo(isEffectActive(), data.isClipping()));
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), position_, std::move(data));
  }

protected:
  // Unused - render() above calls dsp_.applyEffect() directly (it needs
  // the extra had_real_audio argument applyEffect(AudioBuffer&) alone
  // doesn't carry), but EffectTrackState still requires an override.
  void applyEffect(AudioBuffer &) override { }

private:
  SphericalPosition position_;
  TapeDegradationDsp dsp_;
};

class TapeDegradationVoiceState : public EffectVoiceState {
public:
  // note_coord: the real note coordinate (forwarded from
  // TapeDegradation::playNote()) - unlike the track-attached class above,
  // this one is constructed fresh per note-on, so it always has one.
  TapeDegradationVoiceState(const ChannelConfiguration & channel_config, const SphericalPosition & position,
                             const TapeTransportParams & transport_params,
                             float saturationDriveDB, float lowCutHz, float hfRolloffHz, float headBumpHz, float headBumpGainDB, float mix,
                             float spinUpMs, float spinUpDepthCents, float spinDownMs, float droopDepthCents,
                             const NoteCoordinate & note_coord = {})
    : EffectVoiceState(channel_config), position_(position),
      dsp_(channel_config.getAudioOutSampleRate(), seedFromCoord(note_coord), transport_params,
           saturationDriveDB, lowCutHz, hfRolloffHz, headBumpHz, headBumpGainDB, mix,
           spinUpMs, spinUpDepthCents, spinDownMs, droopDepthCents) {
    dsp_.noteOn(); // spin-up starts immediately - this construction *is* note-on
  }

  AudioBuffer render(int frames) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto raw = renderChildren(frames, reduced_config);
    bool had_real_audio = raw.hasChannel(Channel::Main);
    auto data = ensureMainChannel(std::move(raw), frames);
    dsp_.applyEffect(data, had_real_audio);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), position_, std::move(data));
  }

  // A 2Lxx/2Rxx azimuth slide targeting the note this instance wraps
  // should still be audible through it - recurse into children as usual
  // (VoiceState::adjustAzimuth()'s default; harmless on children even
  // though they're mono-reduced and never spatially encode themselves)
  // AND keep this instance's own captured re-encode position in sync,
  // mirroring InstrumentVoice::adjustAzimuth() updating its own secondary
  // floor_position_ copy the same way.
  void adjustAzimuth(float delta) override {
    EffectVoiceState::adjustAzimuth(delta);
    position_.azimuth += delta;
  }

  // Note-off: begins spin-down (hiss fade + pitch droop, see
  // TapeDegradationDsp::applyEffect()) rather than cutting instantly.
  // Children still get their own normal stopNote() (their own envelope's
  // release, entirely independent of this - see dsp/TapeTransport.h's
  // own doc comment on why this never wraps/duplicates the instrument's
  // ADSR).
  void stopNote() override {
    VoiceState::stopNote();
    dsp_.noteOff();
  }

  // fastRelease()/killNote(): a forced reclaim (same-identity retrigger,
  // SF2 exclusive-class choke) - still spin-down, since the delay line
  // still needs *some* time to drain regardless (see DelayLineTail), but
  // compressed (TapeDegradationDsp::noteOffFast()) rather than the full
  // musical spin-down, mirroring EnvelopeFilterDsp::fastRelease()'s own
  // compressed release for the identical reason.
  void fastRelease() override {
    VoiceState::fastRelease();
    dsp_.noteOffFast();
  }

  void killNote() override {
    VoiceState::killNote(); // kill children instantly
    dsp_.noteOffFast();
  }

  // The actual fix for "voice cuts off abruptly": this instance's own
  // tape-machine state (still spinning down, or the wow/flutter delay
  // line still draining - see TapeDegradationDsp::isSilent()) keeps it
  // alive even once every child has gone fully inactive, exactly
  // mirroring EnvelopeFilterVoiceState's own isActive() override
  // (effects/EnvelopeFilter.cpp) for the identical "my own state, not
  // just my children, decides when I'm really done" reason. This is what
  // InstrumentTrackState::renderVoices()/clearFinishedVoices() already
  // consult - no changes needed there at all.
  bool isActive() const override {
    return VoiceState::isActive() || !dsp_.isSilent();
  }

protected:
  void applyEffect(AudioBuffer &) override { } // see TapeDegradationTrackState's own identical comment

private:
  SphericalPosition position_;
  TapeDegradationDsp dsp_;
};

}

TapeTransportParams
TapeDegradation::buildTransportParams() const {
  TapeTransportParams p;
  p.wowRateHz = wowRateHz_;
  p.wowDepthCents = wowDepthCents_;
  p.wowLocked = wowLocked_;
  p.wowLockedRateHz = wowLockedRateHz_;
  p.flutterRateHz = flutterRateHz_;
  p.flutterDepthCents = flutterDepthCents_;
  p.healthRateHz = healthRateHz_;
  p.healthSensitivity = healthSensitivity_;
  p.hissLevelDB = hissLevelDB_;
  p.dropoutRateHz = dropoutRateHz_;
  p.dropoutDepthDB = dropoutDepthDB_;
  p.dropoutDurationMs = dropoutDurationMs_;
  p.clickRateHz = clickRateHz_;
  p.clickGainDB = clickGainDB_;
  p.ampFlutterDepth = ampFlutterDepth_;
  p.rumbleLevelDB = rumbleLevelDB_;
  p.rumbleHz = rumbleHz_;
  p.decayMode = decayMode_;
  p.decayRatePerMinute = decayRatePerMinute_;
  p.breathingAmount = breathingAmount_;
  p.hissLevelDependent = hissLevelDependent_;
  p.breathingAttackMs = breathingAttackMs_;
  p.breathingReleaseMs = breathingReleaseMs_;
  return p;
}

std::unique_ptr<TrackState>
TapeDegradation::createState(const ChannelConfiguration & channel_config) const {
  // getInternalId(), 0, 0 - the track's own identity, its own coordinate
  // (see TapeDegradationTrackState's own doc comment above and
  // NoteCoordinate.h's on why getInternalId() is a safe coordinate
  // component within one run) - no note event to key off here.
  return make_unique<TapeDegradationTrackState>(channel_config, getPosition(), buildTransportParams(),
                                                 saturationDriveDB_, lowCutHz_, hfRolloffHz_, headBumpHz_, headBumpGainDB_, mix_,
                                                 swoopTimeMs_, swoopStartCents_, spinDownMs_, droopDepthCents_,
                                                 NoteCoordinate(getInternalId(), 0, 0));
}

std::unique_ptr<VoiceState>
TapeDegradation::createVoiceState(const ChannelConfiguration & channel_config) const {
  // No position known through this path (see the header's own comment on
  // when this is actually reached) - the "no direction authored" sentinel,
  // same convention as an unauthored track-attached position.
  return make_unique<TapeDegradationVoiceState>(channel_config, SphericalPosition{}, buildTransportParams(),
                                                 saturationDriveDB_, lowCutHz_, hfRolloffHz_, headBumpHz_, headBumpGainDB_, mix_,
                                                 swoopTimeMs_, swoopStartCents_, spinDownMs_, droopDepthCents_);
}

std::unique_ptr<VoiceState>
TapeDegradation::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune,
                           float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  // Mirrors Track::playNote()'s own default body (Track.h) exactly,
  // except the group node it builds is a real TapeDegradationVoiceState
  // (carrying the note's real position) rather than the generic
  // createVoiceState()-built wrapper the default uses.
  auto group = make_unique<TapeDegradationVoiceState>(config, position, buildTransportParams(),
                                                        saturationDriveDB_, lowCutHz_, hfRolloffHz_, headBumpHz_, headBumpGainDB_, mix_,
                                                        swoopTimeMs_, swoopStartCents_, spinDownMs_, droopDepthCents_, note_coord);
  auto child_config = getChildChannelConfiguration(config);
  for (auto & child : getChildren()) {
    auto voice = child->playNote(child_config, position, frequency, detune, velocity, note_value, sends, note_coord, needs_decorrelation);
    if (voice.get()) group->addChild(child->getInternalId(), std::move(voice));
  }
  return group;
}

void
TapeDegradation::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  preset_ = input.getText("preset", "tape");
  const auto & preset = getTapeDegradationPreset(preset_);

  azimuth_ = input.getFloat("azimuth");
  distance_ = input.getFloat("distance");
  elevation_ = input.getFloat("elevation");
  extent_ = input.getFloat("extent", -1.0f);

  wowRateHz_ = input.getFloat("wowRateHz", preset.wowRateHz);
  wowDepthCents_ = input.getFloat("wowDepthCents", preset.wowDepthCents);
  wowLocked_ = input.getBool("wowLocked", preset.wowLocked);
  wowLockedRateHz_ = input.getFloat("wowLockedRateHz", preset.wowLockedRateHz);
  flutterRateHz_ = input.getFloat("flutterRateHz", preset.flutterRateHz);
  flutterDepthCents_ = input.getFloat("flutterDepthCents", preset.flutterDepthCents);
  healthRateHz_ = input.getFloat("healthRateHz", preset.healthRateHz);
  healthSensitivity_ = input.getFloat("healthSensitivity", preset.healthSensitivity);
  hissLevelDB_ = input.getFloat("hissLevelDB", preset.hissLevelDB);
  dropoutRateHz_ = input.getFloat("dropoutRateHz", preset.dropoutRateHz);
  dropoutDepthDB_ = input.getFloat("dropoutDepthDB", preset.dropoutDepthDB);
  dropoutDurationMs_ = input.getFloat("dropoutDurationMs", preset.dropoutDurationMs);
  clickRateHz_ = input.getFloat("clickRateHz", preset.clickRateHz);
  clickGainDB_ = input.getFloat("clickGainDB", preset.clickGainDB);
  saturationDriveDB_ = input.getFloat("saturationDriveDB", preset.saturationDriveDB);
  lowCutHz_ = input.getFloat("lowCutHz", preset.lowCutHz);
  hfRolloffHz_ = input.getFloat("hfRolloffHz", preset.hfRolloffHz);
  headBumpHz_ = input.getFloat("headBumpHz", preset.headBumpHz);
  headBumpGainDB_ = input.getFloat("headBumpGainDB", preset.headBumpGainDB);
  mix_ = input.getFloat("mix", preset.mix);

  ampFlutterDepth_ = input.getFloat("ampFlutterDepth", preset.ampFlutterDepth);
  swoopStartCents_ = input.getFloat("swoopStartCents", preset.swoopStartCents);
  swoopTimeMs_ = input.getFloat("swoopTimeMs", preset.swoopTimeMs);
  spinDownMs_ = input.getFloat("spinDownMs", preset.spinDownMs);
  droopDepthCents_ = input.getFloat("droopDepthCents", preset.droopDepthCents);

  rumbleLevelDB_ = input.getFloat("rumbleLevelDB", preset.rumbleLevelDB);
  rumbleHz_ = input.getFloat("rumbleHz", preset.rumbleHz);

  decayMode_ = input.getBool("decayMode", preset.decayMode);
  decayRatePerMinute_ = input.getFloat("decayRatePerMinute", preset.decayRatePerMinute);

  breathingAmount_ = input.getFloat("breathingAmount", preset.breathingAmount);
  hissLevelDependent_ = input.getFloat("hissLevelDependent", preset.hissLevelDependent);
  breathingAttackMs_ = input.getFloat("breathingAttackMs", preset.breathingAttackMs);
  breathingReleaseMs_ = input.getFloat("breathingReleaseMs", preset.breathingReleaseMs);
}

void
TapeDegradation::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("preset", preset_, std::string("tape"));
  const auto & preset = getTapeDegradationPreset(preset_);

  output.set("azimuth", azimuth_);
  output.set("distance", distance_);
  output.set("elevation", elevation_);
  output.set("extent", extent_, -1.0f);

  output.set("wowRateHz", wowRateHz_, preset.wowRateHz);
  output.set("wowDepthCents", wowDepthCents_, preset.wowDepthCents);
  output.set("wowLocked", wowLocked_, preset.wowLocked);
  output.set("wowLockedRateHz", wowLockedRateHz_, preset.wowLockedRateHz);
  output.set("flutterRateHz", flutterRateHz_, preset.flutterRateHz);
  output.set("flutterDepthCents", flutterDepthCents_, preset.flutterDepthCents);
  output.set("healthRateHz", healthRateHz_, preset.healthRateHz);
  output.set("healthSensitivity", healthSensitivity_, preset.healthSensitivity);
  output.set("hissLevelDB", hissLevelDB_, preset.hissLevelDB);
  output.set("dropoutRateHz", dropoutRateHz_, preset.dropoutRateHz);
  output.set("dropoutDepthDB", dropoutDepthDB_, preset.dropoutDepthDB);
  output.set("dropoutDurationMs", dropoutDurationMs_, preset.dropoutDurationMs);
  output.set("clickRateHz", clickRateHz_, preset.clickRateHz);
  output.set("clickGainDB", clickGainDB_, preset.clickGainDB);
  output.set("saturationDriveDB", saturationDriveDB_, preset.saturationDriveDB);
  output.set("lowCutHz", lowCutHz_, preset.lowCutHz);
  output.set("hfRolloffHz", hfRolloffHz_, preset.hfRolloffHz);
  output.set("headBumpHz", headBumpHz_, preset.headBumpHz);
  output.set("headBumpGainDB", headBumpGainDB_, preset.headBumpGainDB);
  output.set("mix", mix_, preset.mix);

  output.set("ampFlutterDepth", ampFlutterDepth_, preset.ampFlutterDepth);
  output.set("swoopStartCents", swoopStartCents_, preset.swoopStartCents);
  output.set("swoopTimeMs", swoopTimeMs_, preset.swoopTimeMs);
  output.set("spinDownMs", spinDownMs_, preset.spinDownMs);
  output.set("droopDepthCents", droopDepthCents_, preset.droopDepthCents);

  output.set("rumbleLevelDB", rumbleLevelDB_, preset.rumbleLevelDB);
  output.set("rumbleHz", rumbleHz_, preset.rumbleHz);

  output.set("decayMode", decayMode_, preset.decayMode);
  output.set("decayRatePerMinute", decayRatePerMinute_, preset.decayRatePerMinute);

  output.set("breathingAmount", breathingAmount_, preset.breathingAmount);
  output.set("hissLevelDependent", hissLevelDependent_, preset.hissLevelDependent);
  output.set("breathingAttackMs", breathingAttackMs_, preset.breathingAttackMs);
  output.set("breathingReleaseMs", breathingReleaseMs_, preset.breathingReleaseMs);
}
