#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "Note.h"
#include "EnvelopeGenerator.h"

class InstrumentVoice {
 public:
  InstrumentVoice(int _identifier)
    : identifier(_identifier) { }
  InstrumentVoice(int _identifier, const Envelope & _amp_envelope)
    : identifier(_identifier), amp_envelope(_amp_envelope) {

  }
  InstrumentVoice(int _identifier, const Envelope & _amp_envelope, const Envelope & _mod_envelope)
    : identifier(_identifier), amp_envelope(_amp_envelope), mod_envelope(_mod_envelope) {
    
  }
  virtual ~InstrumentVoice() { }
  
  virtual void render(float * buffer, size_t frames, size_t offset = 0) = 0;

  virtual void stopNote() {
    ampenv.nextSegment(EnvelopeGenerator::SUSTAIN);
    modenv.nextSegment(EnvelopeGenerator::SUSTAIN);
  }

  virtual void killNote() {
    ampenv.nextSegment(EnvelopeGenerator::DONE);
    modenv.nextSegment(EnvelopeGenerator::DONE);

    freq = 0.0f;
  }
  
  virtual void playNote(float _frequency, float velocity, float _delay, float _detune) {
    int midiVelocity = int(velocity * 127);
    if (midiVelocity > 127) midiVelocity = 127;
    ampenv = EnvelopeGenerator(amp_envelope, 0, midiVelocity, true, 44100, _delay);
    modenv = EnvelopeGenerator(mod_envelope, 0, midiVelocity, false, 44100, _delay);
	
    freq = _frequency;
    detune = _detune;
        
    setGainDB(-gainToDecibels(1.0f / velocity));
    
    sourceSamplePosition = 0;
  }

  virtual bool isPlaying() const { return freq != 0 && !ampenv.isDone(); }

  float updateADSR() {
    float gain = ampenv.getLevel();
    ampenv.process(1);
    return gain;
  }

  void setIdentifier(int id) { identifier = id; }
  int getIdentifier() const { return identifier; }
  
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

  void stepForward() {
    sourceSamplePosition += freq;
  }

  EnvelopeGenerator ampenv, modenv;
  double sourceSamplePosition = 0.0;

private:
  int identifier;
  float freq = 0.0f, detune = 0.0f;
  float noteGainDB = 0.0f;
  Envelope amp_envelope, mod_envelope;
};

#endif
