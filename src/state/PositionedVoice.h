#ifndef _POSITIONEDVOICE_H_
#define _POSITIONEDVOICE_H_

#include "VoiceState.h"
#include "../ambisonic/SphericalPosition.h"
#include "../ambisonic/AmbisonicEncoding.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"
#include "../dsp/FractionalDelayLine.h"
#include "../dsp/Biquad.h"
#include "../dsp/FilterType.h"
#include "../ambisonic/FloorReflection.h"

#include <algorithm>
#include <cmath>
#include <vector>

// A leaf voice's spatial identity: position, Send Main/A/B, floor
// reflection, encodePosition() - everything a sound needs to place itself
// in the ambisonic bus, independent of whether it has a pitch at all.
// InstrumentVoice (frequency/detune/phase-accumulator - genuinely
// pitch-specific) derives from this for its own pitched leaves
// (OscillatorVoice, SoundFontVoice, ...); a leaf with no pitch concept
// (SampleTrack.cpp's SampleClipVoice) derives from this directly instead,
// so it never has to ignore or repurpose machinery sized for something it
// isn't.
class PositionedVoice : public VoiceState {
 public:
  PositionedVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {})
    : VoiceState(channel_config),
      note_hash_coord_(note_coord.toHashCoord()),
      position_(position),
      sends_(sends),
      floor_absorption_filter_(FilterType::lowpass)
  {
    if (channel_config.getFloorReflectionEnabled()) initFloorReflection(channel_config);
  }

  SphericalPosition getPosition() const { return position_; }
  const SendLevels & getSends() const { return sends_; }

  // Dry-signal distance attenuation only (1/distance) - see
  // encodePosition()'s own doc comment for why the room's shared send bus
  // deliberately doesn't scale by this.
  float getDistanceGain() const { return distanceGain(position_.distance); }

  // YLxx/YRxx azimuth slide and the live Send Main/A/B knobs - see
  // VoiceState.h's own doc comments on why these recurse into children by
  // default there and are overridden here instead: every leaf voice
  // (pitched or not) has a real position/sends to move live.
  void adjustAzimuth(float delta) override {
    position_.azimuth += delta;
    if (floor_reflection_active_) floor_position_.azimuth += delta;
  }
  void adjustSendMain(float s) override { sends_.main = s; }
  void adjustSendA(float s) override { sends_.a = s; }
  void adjustSendB(float s) override { sends_.b = s; }

  // See VoiceState::getOwnLoudnessFactor()/getNoteValue()'s own doc
  // comments - every leaf voice overrides both with its own stored value;
  // neither is pitch-specific, so both live here rather than in
  // InstrumentVoice.
  float getOwnLoudnessFactor() const override { return velocity_; }
  int getNoteValue() const override { return note_value_; }

  // Builds this voice's own regular-channel accumulator for its real
  // ChannelConfiguration and spatially encodes `dry` into those regular
  // channels via this voice's own position, smoothly gain-interpolated
  // block to block by encoder_ - one persistent instance per voice.
  // `dry` is expected to carry <note gain> only, not distance attenuation
  // - applied here instead, folded into the same per-channel gains array
  // as getSends().main. AuxA/AuxB (the shared send bus) deliberately
  // don't attenuate with distance - an instrument's contribution to the
  // room's reverb doesn't diminish just because the listener is farther
  // from that one source.
  AudioBuffer encodePosition(const float * dry, int frames) {
    auto & sends = getSends();
    bool has_main = sends.main > 0.0f;
    AudioBuffer data(has_main ? getChannelConfiguration().numberOfChannels() : 0, sends.a > 0.0f, sends.b > 0.0f, frames);
    data.zero();

    if (has_main) {
      auto gains = computeAmbisonicGains(getPosition());
      float main_gain = sends.main * getDistanceGain();
      for (auto & g : gains) g *= main_gain;
      encoder_.encodeBlock(data, dry, frames, gains);

      // Geometry-derived floor reflection - a second, independently
      // directed and delayed copy of the same dry signal, encoded
      // through its own AmbisonicVoiceEncoder instance. Shares main_gain
      // (Send Main and 1/distance both apply to the reflection too),
      // scaled further by floor_gain_ratio_. Never touches AuxA/AuxB -
      // the reflection is not a send.
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

  int64_t note_hash_coord_;

 protected:
  void setGainDB(float db) { noteGainDB_ = db; }
  float getGainDB() const { return noteGainDB_; }

  int note_value_ = -1;
  float velocity_ = 0.0f;

 private:
  // Every value the floor reflection needs follows from the song's ear
  // height and this voice's own position_'s distance/elevation, both
  // fixed for this voice's whole lifetime (only azimuth ever changes
  // post-construction - see adjustAzimuth() above), so it's computed
  // once, here, never recomputed or smoothed per block.
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

    float absorption = channel_config.getGroundAbsorption();
    float cutoff_hz = 20000.0f * (1.0f - absorption) * (1.0f - absorption);
    float fc_normalized = std::min(cutoff_hz / sample_rate, 0.499f);
    floor_absorption_filter_.set(fc_normalized, 0.707f);

    floor_delay_line_.resize(floorReflectionMaxDelaySamples(earHeight, sample_rate));

    floor_reflection_active_ = true;
  }

  float noteGainDB_ = 0.0f;
  SphericalPosition position_;
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
