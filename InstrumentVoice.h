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
    
  void playNote(float frequency, float velocity) override {
    TrackState::playNote(frequency, velocity);
    
    if (freq_ == 0.0f) {
      setGainDB(-gainToDecibels(1.0f / velocity));
    }
    freq_ = frequency * detune_;
  }

  bool isPlaying() const override { return freq_ != 0.0f; }
  
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

private:
  float freq_ = 0.0f;
  float noteGainDB_ = 0.0f;
  float azimuth_;
  float detune_;
};

#endif
