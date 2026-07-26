#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "TrackState.h"
#include "SphericalPosition.h"
#include "AmbisonicEncoding.h"
#include "SendLevels.h"

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
      sends_(sends)
  {

  }

  void killNote() override {
    TrackState::killNote();
    freq_ = 0.0f;
  }
  
  void stopNote() override { killNote(); }
    
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
  // reverb/chorus bus (SendA/SendB) deliberately does NOT scale by this: an
  // instrument's contribution to the room's reverb doesn't diminish just
  // because the listener is farther from that one source. Applied uniformly
  // regardless of bus type (previously this was ambisonic-only, baked into
  // computeAmbisonicGains/PositionalMixer::encode - see AmbisonicEncoding.h).
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
  // SendA/SendB (which deliberately don't attenuate with distance), a
  // bake-then-unbake round trip through every single sample for no actual
  // effect on the sends and an extra multiply-per-sample on the dry side;
  // computing send[k] = dry[k] * getSends().a/b directly, with no division,
  // is both simpler and cheaper now that `dry` was never distance-attenuated
  // to begin with. Gain-interpolated block to block by encoder_ same as
  // before, so a moving source or a distance/Send Main change still
  // smooths, not clicks.
  SampleData encodePosition(const float * dry, int frames) {
    auto & sends = getSends();
    SampleData data(getChannelConfiguration().numberOfChannels(), sends.a > 0.0f, sends.b > 0.0f, frames);
    data.zero();
    auto gains = computeAmbisonicGains(getPosition());
    float main_gain = sends.main * getDistanceGain();
    for (auto & g : gains) g *= main_gain;
    encoder_.encodeBlock(data, dry, frames, gains);

    if (auto * send_a = data.getChannel(Channel::SendA)) {
      for (int i = 0; i < frames; i++) send_a[i] = dry[i] * sends.a;
    }
    if (auto * send_b = data.getChannel(Channel::SendB)) {
      for (int i = 0; i < frames; i++) send_b[i] = dry[i] * sends.b;
    }

    data.setNonZero();
    return data;
  }

  double sourceSamplePosition_;
  int note_value_ = -1;
  float velocity_ = 0.0f;

private:
  float freq_ = 0.0f;
  float noteGainDB_ = 0.0f;
  SphericalPosition position_;
  float detune_;
  SendLevels sends_;
  AmbisonicVoiceEncoder encoder_;
};

#endif
