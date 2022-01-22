#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "TrackState.h"

class InstrumentVoice : public TrackState {
 public:
  InstrumentVoice(unsigned int _outSampleRate) : TrackState(_outSampleRate) { }

  void killNote() override {
    TrackState::killNote();

    freq = 0.0f;
  }
  void stopNote() override { killNote(); }
    
  virtual void playNote(float _frequency, float velocity, float start_phase) {	
    freq = _frequency;
    setGainDB(-gainToDecibels(1.0f / velocity));
    sourceSamplePosition = start_phase * getOutSampleRate();
  }

  bool isPlaying() const override { return freq != 0.0f; }
  bool isReleased() const { return false; }
    
  void setVolume(float volume) {
    setGainDB(gainToDecibels(volume));
  }

  void setGainDB(float db) { noteGainDB = db; }
  float getGainDB() const { return noteGainDB; }

  static inline float gainToDecibels(float gain) {
    return (gain <= .00001f ? -100.f : (float)(20.0 * log10(gain)));
  }

  static inline float decibelsToGain(float db) {
    return (db > -100.f ? powf(10.0f, db * 0.05f) : 0);
  }
  
protected:
  double getSourceSamplePosition() const { return sourceSamplePosition; }

  inline void stepForward(size_t frames) {
    sourceSamplePosition += freq * frames;
  }

  inline float getFrequency() const { return freq; }

  double sourceSamplePosition = 0.0;

private:
  float freq = 0.0f;
  float noteGainDB = 0.0f;
};

#endif
