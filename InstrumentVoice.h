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
    
    switch (adsrstate) {
    case 0:
      if (envelope.getAttack() == 0 || adsrpos >= envelope.getAttack()) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = 1.0f;
      } else {
	adsrvol = (float)adsrpos / envelope.getAttack();
      }
      break;
    case 1:
      if (envelope.getDecay() == 0 || adsrpos >= envelope.getDecay()) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = envelope.getSustain();
      } else {
	adsrvol = 1.0 - ((1.0 - envelope.getSustain()) * (float)adsrpos / envelope.getDecay());
      }
      break;
    case 2:
      adsrvol = envelope.getSustain();
      break;
    case 3:
      if (envelope.getRelease() == 0 || adsrpos >= envelope.getRelease()) {
	adsrstate++;
	adsrvol = 0;
      } else {
	adsrvol = envelope.getSustain() - (envelope.getSustain() * (float)adsrpos / envelope.getRelease());
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
      // float fscaler = (float)WAVESIZE / 44100.0f;
      freq = note.getFrequency(tuning, transpose, detune);
      velocity = note.getVelocityAsFloat();
      adsrstate = 0;
      adsrpos = 0;
      wave_position = 0;
    }
  }

  virtual bool isPlaying() const { return adsrstate < 4 && freq != 0; }

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
