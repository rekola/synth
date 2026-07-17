#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "TrackState.h"

class InstrumentVoice : public TrackState {
 public:
  InstrumentVoice(const ChannelConfiguration & channel_config, float azimuth, float detune, float start_phase)
    : TrackState(channel_config),
      sourceSamplePosition_(start_phase * getChannelConfiguration().getAudioOutSampleRate()),
      azimuth_(azimuth),
      detune_(detune)
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
  float getAzimuth() const { return azimuth_; }
  float getDetune() const { return detune_; }

  void setGainDB(float db) { noteGainDB_ = db; }
  float getGainDB() const { return noteGainDB_; }

  double sourceSamplePosition_;
  int note_value_ = -1;
  float velocity_ = 0.0f;

private:
  float freq_ = 0.0f;
  float noteGainDB_ = 0.0f;
  float azimuth_;
  float detune_;
};

#endif
