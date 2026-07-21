#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "TrackState.h"
#include "SphericalPosition.h"

// distance <= 0 means "no position ever set" (SphericalPosition's default),
// not "at the listener" - treated as no attenuation, same convention
// computeAmbisonicGains's own distance<=0 fallback uses (AmbisonicEncoding.h).
inline float distanceGain(float distance) {
  return distance <= 0.0f ? 1.0f : 1.0f / distance;
}

class InstrumentVoice : public TrackState {
 public:
  InstrumentVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, float start_phase, float send_a = 0.0f, float send_b = 0.0f)
    : TrackState(channel_config),
      sourceSamplePosition_(start_phase * getChannelConfiguration().getAudioOutSampleRate()),
      position_(position),
      detune_(detune),
      send_a_(send_a), send_b_(send_b)
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

  SphericalPosition getPosition() const override { return position_; }

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

  float getSendA() const { return send_a_; }
  float getSendB() const { return send_b_; }

  // Dry-signal distance attenuation only (1/distance) - the room's shared
  // reverb/chorus bus (SendA/SendB) deliberately does NOT scale by this: an
  // instrument's contribution to the room's reverb doesn't diminish just
  // because the listener is farther from that one source. Applied uniformly
  // regardless of bus type (previously this was ambisonic-only, baked into
  // computeAmbisonicGains/PositionalMixer::encode - see AmbisonicEncoding.h).
  float getDistanceGain() const { return distanceGain(position_.distance); }

  double sourceSamplePosition_;
  int note_value_ = -1;
  float velocity_ = 0.0f;

private:
  float freq_ = 0.0f;
  float noteGainDB_ = 0.0f;
  SphericalPosition position_;
  float detune_;
  float send_a_, send_b_;
};

#endif
