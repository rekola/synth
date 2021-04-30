#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "Note.h"
#include "Envelope.h"

class InstrumentVoice {
 public:
  InstrumentVoice(int _identifier) : identifier(_identifier) { }
  virtual ~InstrumentVoice() { }
  
  virtual void render(float * buffer, size_t frames) = 0;
  
  float updateADSR(const Envelope & envelope) {
    float adsrvol = 0;

    int attack = int(envelope.getAttack() * 44100 * 5);
    int decay = int(envelope.getDecay() * 44100 * 5);
    float sustain = envelope.getSustain();
    int release = int(envelope.getRelease() * 44100 * 5);
	  
    switch (adsrstate) {
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
      adsrvol = sustain;
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
  }

  virtual void stopNote() {
    adsrstate = 3;
    adsrpos = 0;
  }
  
  virtual void playNote(Tuning tuning, Note note, int transpose, int detune) {
    if (note.isOff()) {
      stopNote();
    } else if (note.isDefined()) {
      freq = note.getFrequency(tuning, transpose, detune);
      velocity = note.getVelocityAsFloat();
      adsrstate = 0;
      adsrpos = 0;
      wave_position = 0;
    }
  }

  virtual bool isPlaying() const { return adsrstate < 4 && freq != 0; }

  void setIdentifier(int id) { identifier = id; }
  int getIdentifier() const { return identifier; }
  float getVelocity() const { return velocity; }

protected:
  float getWavePosition() const { return wave_position; }

  void stepForward() {
    wave_position += freq;
  }

  float wave_position = 0; // position in the waveform
  float freq = 0; // current frequency

 private:
  int identifier;
  float velocity = 1.0f;
  // adsr state
  int adsrstate = 0, adsrpos = 0;
};

#endif
