#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "TrackState.h"
#include "SphericalPosition.h"
#include "AmbisonicEncoding.h"
#include "SendLevels.h"
#include "dsp/FractionalDelayLine.h"
#include "dsp/Biquad.h"
#include "dsp/FilterType.h"
#include "FloorReflection.h"

#include <algorithm>
#include <cmath>
#include <vector>

// distance <= 0 means "no position ever set" (SphericalPosition's default),
// not "at the listener" - treated as no attenuation, same convention
// computeAmbisonicGains's own distance<=0 fallback uses (AmbisonicEncoding.h).
inline float distanceGain(float distance) {
  return distance <= 0.0f ? 1.0f : 1.0f / distance;
}

class InstrumentVoice : public TrackState {
 public:
  InstrumentVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, float start_phase, const SendLevels & sends = {})
    : TrackState(channel_config),
      sourceSamplePosition_(start_phase * getChannelConfiguration().getAudioOutSampleRate()),
      position_(position),
      detune_(detune),
      sends_(sends),
      floor_absorption_filter_(FilterType::lowpass)
  {
    if (channel_config.getFloorReflectionEnabled()) initFloorReflection(channel_config);
  }

  void killNote() override {
    TrackState::killNote();
    freq_ = 0.0f;
  }
  
  void stopNote() override { killNote(); }

  // Every non-SF2 leaf already cuts instantly on stopNote() (via
  // killNote() above) - no separate release phase to shorten, so
  // fastRelease() just aliases it. SoundFontVoice overrides this with a
  // real short release instead (see TrackState::fastRelease()'s comment).
  void fastRelease() override { stopNote(); }
    
  void playNote(float frequency, float velocity, int note_value) override {
    TrackState::playNote(frequency, velocity, note_value);

    if (freq_ == 0.0f) {
      setGainDB(-gainToDecibels(1.0f / velocity));
    }
    freq_ = frequency * detune_;
    note_value_ = note_value;
    velocity_ = velocity;
  }

  bool isActive() const override { return freq_ != 0.0f; }

  int getNoteValue() const override { return note_value_; }

  SphericalPosition getPosition() const { return position_; }

  // Raw performance velocity (0..1), deliberately NOT decibelsToGain(getGainDB())
  // - getGainDB() can carry extra per-instrument mixing gain (e.g.
  // SoundFontVoice bakes its SF2 region's attenuation into it), which would
  // otherwise dilute a full-velocity hit's LED brightness well below "full"
  // for quieter-by-design patches. Subclasses with their own envelope
  // (SoundFontVoice) multiply this by their normalized envelope level
  // instead of substituting a gain-derived value.
  float getOwnLoudnessFactor() const override { return velocity_; }

protected:
  double getSourceSamplePosition() const { return sourceSamplePosition_; }

  inline void stepForward(int frames) {
    sourceSamplePosition_ += freq_ * frames;
  }

  float getFrequency() const { return freq_; }
  float getAzimuth() const { return position_.azimuth; }
  float getDetune() const { return detune_; }

  void setGainDB(float db) { noteGainDB_ = db; }
  float getGainDB() const { return noteGainDB_; }

  const SendLevels & getSends() const { return sends_; }

  // Dry-signal distance attenuation only (1/distance) - the room's shared
  // reverb/chorus bus (AuxA/AuxB) deliberately does NOT scale by this: an
  // instrument's contribution to the room's reverb doesn't diminish just
  // because the listener is farther from that one source. Applied uniformly
  // regardless of bus type (previously this was ambisonic-only, baked into
  // computeAmbisonicGains/encodePosition() below - see AmbisonicEncoding.h).
  float getDistanceGain() const { return distanceGain(position_.distance); }

  // Builds this voice's own regular-channel accumulator for its real
  // ChannelConfiguration (MONO's W, or AMBISONIC's W/Y/Z/X[+Acn4-8] - no
  // longer reduced to MONO before construction, see AmbisonicEncoding.h's
  // now-removed reduceForPositionalGroup) and spatially encodes `dry` into
  // those regular channels via this voice's own position (getPosition() -
  // a subclass like SoundFontVoice bakes any adjustment of its own, e.g.
  // its SF2 region's pan, straight into position_/sends_ once at
  // construction time, since none of that ever changes after - see
  // SoundFont.cpp - rather than recomputing it on every call via a virtual
  // override), smoothly gain-interpolated block to block by encoder_ - one
  // persistent instance per voice, replacing the old external
  // PositionalMixer's per-id map (this voice already IS the stable, per-note
  // object that map used to key by pointer, so owning the state directly
  // here needs no separate cleanup/remove() step - it just dies with the
  // voice).
  //
  // `dry` is expected to carry <note gain> only - NOT distance attenuation
  // (unlike this function's own former contract): distance attenuation is
  // applied here instead, folded into the same per-channel gains array as
  // getSends().main, both computed once per block rather than per sample -
  // callers used to bake getDistanceGain() into every sample of `dry`
  // themselves and this function then divided it back out again for
  // AuxA/AuxB (which deliberately don't attenuate with distance), a
  // bake-then-unbake round trip through every single sample for no actual
  // effect on the sends and an extra multiply-per-sample on the dry side;
  // computing send[k] = dry[k] * getSends().a/b directly, with no division,
  // is both simpler and cheaper now that `dry` was never distance-attenuated
  // to begin with. Gain-interpolated block to block by encoder_ same as
  // before, so a moving source or a distance/Send Main change still
  // smooths, not clicks.
  SampleData encodePosition(const float * dry, int frames) {
    auto & sends = getSends();
    bool has_main = sends.main > 0.0f;
    SampleData data(has_main ? getChannelConfiguration().numberOfChannels() : 0, sends.a > 0.0f, sends.b > 0.0f, frames);
    data.zero();

    if (has_main) {
      auto gains = computeAmbisonicGains(getPosition());
      float main_gain = sends.main * getDistanceGain();
      for (auto & g : gains) g *= main_gain;
      encoder_.encodeBlock(data, dry, frames, gains);

      // Geometry-derived floor reflection - a second, independently
      // directed and delayed copy of the same dry signal, encoded
      // through its own AmbisonicVoiceEncoder instance. Deliberately
      // shares main_gain (Send Main and 1/distance both apply to the
      // reflection too - it's a phenomenon of the dry/Main path only,
      // see getSends()'s own doc comment on why sends never touch this),
      // scaled further by floor_gain_ratio_ (the reflection's own
      // strength relative to the direct path). Never touches AuxA/AuxB
      // below - the reflection is not a send.
      if (floor_reflection_active_) {
        if (static_cast<int>(floor_scratch_.size()) != frames) floor_scratch_.resize(static_cast<size_t>(frames));
        for (int i = 0; i < frames; i++) {
          floor_delay_line_.write(dry[i]);
          floor_scratch_[static_cast<size_t>(i)] = floor_absorption_filter_.process(floor_delay_line_.read(floor_delay_samples_));
        }

        auto floor_gains = computeAmbisonicGains(floor_position_);
        float floor_main_gain = main_gain * floor_gain_ratio_;
        for (auto & g : floor_gains) g *= floor_main_gain;
        floor_encoder_.encodeBlock(data, floor_scratch_.data(), frames, floor_gains);
      }
    }

    if (auto * aux_a = data.getChannel(Channel::AuxA)) {
      for (int i = 0; i < frames; i++) aux_a[i] = dry[i] * sends.a;
    }
    if (auto * aux_b = data.getChannel(Channel::AuxB)) {
      for (int i = 0; i < frames; i++) aux_b[i] = dry[i] * sends.b;
    }

    return data;
  }

  double sourceSamplePosition_;
  int note_value_ = -1;
  float velocity_ = 0.0f;

private:
  // Every value the floor reflection needs - delay, gain, and the
  // reflected direction - follows directly from the song's ear height
  // and this voice's own position_, both fixed for this voice's whole
  // lifetime (position_ never changes after construction, see this
  // class's own comments above) - so it's all computed once, here, never
  // recomputed or smoothed per block, via the pure (and independently
  // testable) geometry in FloorReflection.h. distance <= 0 (no position
  // ever set) leaves floor_reflection_active_ false, same "nothing to
  // attach a direction to" reasoning computeAmbisonicGains() itself
  // already uses.
  void initFloorReflection(const ChannelConfiguration & channel_config) {
    if (position_.distance <= 0.0f) return;

    float sample_rate = static_cast<float>(channel_config.getAudioOutSampleRate());
    float earHeight = channel_config.getEarHeight();

    auto geom = computeFloorReflectionGeometry(earHeight, position_.distance, position_.elevation,
                                                channel_config.getFloorReflectionStrength(), sample_rate);
    floor_delay_samples_ = geom.delaySamples;
    floor_gain_ratio_ = geom.gainRatio;
    floor_position_ = position_;
    floor_position_.elevation = geom.elevationDegrees;

    // groundAbsorption in [0,1] mapped to a lowpass cutoff - fully open
    // (20kHz, effectively inaudible filtering) at 0, tightening toward
    // ~1kHz as absorption approaches 1. A first-pass shape, not derived
    // from measured material data.
    float absorption = channel_config.getGroundAbsorption();
    float cutoff_hz = 20000.0f * (1.0f - absorption) * (1.0f - absorption);
    float fc_normalized = std::min(cutoff_hz / sample_rate, 0.499f);
    floor_absorption_filter_.set(fc_normalized, 0.707f); // Q ~ Butterworth-flat - gentle rolloff, no resonant peak

    floor_delay_line_.resize(floorReflectionMaxDelaySamples(earHeight, sample_rate));

    floor_reflection_active_ = true;
  }

  float freq_ = 0.0f;
  float noteGainDB_ = 0.0f;
  SphericalPosition position_;
  float detune_;
  SendLevels sends_;
  AmbisonicVoiceEncoder encoder_;

  bool floor_reflection_active_ = false;
  float floor_delay_samples_ = 0.0f;
  float floor_gain_ratio_ = 0.0f;
  SphericalPosition floor_position_;
  FractionalDelayLine floor_delay_line_;
  Biquad<float> floor_absorption_filter_;
  AmbisonicVoiceEncoder floor_encoder_;
  std::vector<float> floor_scratch_;
};

#endif
