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
  virtual ~InstrumentVoice() { }
  
  virtual void render(float * buffer, size_t frames, size_t offset = 0) = 0;

  virtual void stopNote() {
    ampenv.nextSegment(EnvelopeGenerator::SUSTAIN);
    modenv.nextSegment(EnvelopeGenerator::SUSTAIN);

    if (adsrstate == -1) { // from delay, jump directly to end
      adsrstate = 4;
    } else if (adsrstate < 2) { // otherwise, wait till sustain
      is_stopped = true;
    } else if (adsrstate == 2) { // from sustain to release
      adsrstate = 3;
      adsrpos = 0;
    }
  }

  virtual void killNote() {
    ampenv.nextSegment(EnvelopeGenerator::DONE);
    modenv.nextSegment(EnvelopeGenerator::DONE);

    adsrstate = 4;
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
    
    adsrstate = 0;
    adsrpos = 0;
    wave_position = 0;
    is_stopped = false;
  }

  virtual bool isPlaying() const { return adsrstate < 4 && freq != 0; }

  float updateADSR() {
#if 1
    float gain = ampenv.getLevel();
    ampenv.process(1);
    return gain;
#else
    int del = 0; // int((delay + amp_envelope.getDelay()) * 44100);
    int attack = int(amp_envelope.getAttack() * 44100);
    int decay = int(amp_envelope.getDecay() * 44100);
    float sustain = amp_envelope.getSustain();
    int release = int(amp_envelope.getRelease() * 44100);
	  
    float adsrvol = 0;

    switch (adsrstate) {
    case -1:
      if (del == 0 || adsrpos >= del) {
	adsrstate++;
	adsrpos = 0;
      } else {
	adsrvol = 0.0f;
      }
      break;
    case 0:
      if (attack == 0 || adsrpos >= attack) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = 1.0f;
      } else {
	adsrvol = (float)adsrpos / attack;
      }
      break;
    case 1:
      if (decay == 0 || adsrpos >= decay) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = sustain;
      } else {
	adsrvol = 1.0 - ((1.0 - sustain) * (float)adsrpos / decay);
      }
      break;
    case 2:
      if (is_stopped) {
	adsrstate++;
      } else {
	adsrvol = sustain;
      }
      break;
    case 3:
      if (release == 0 || adsrpos >= release) {
	adsrstate++;
	adsrvol = 0;
      } else {
	adsrvol = sustain - (sustain * (float)adsrpos / release);
      }
      break;
    default:
      adsrvol = 0;
      break;
    }
    adsrpos++;

    return adsrvol;
#endif
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
  float getWavePosition() const { return wave_position; }

  void stepForward() {
    wave_position += freq;
  }

  EnvelopeGenerator ampenv, modenv;

private:
  int identifier;
  float wave_position = 0.0f, freq = 0.0f, detune = 0.0f;
  float noteGainDB = 0.0f;

  // adsr state
  int adsrstate = 0, adsrpos = 0;
  Envelope amp_envelope, mod_envelope;
  bool is_stopped = false;
};

#endif
