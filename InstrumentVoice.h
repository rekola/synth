#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "Note.h"
#include "Envelope.h"

class InstrumentVoice {
 public:
  InstrumentVoice() { }
  virtual ~InstrumentVoice() { }
  
  float getFphase() const { return fphase; }

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

  virtual void playNote(Note note, int transpose, int detune) {
    float _velocity = note.getVelocityAsFloat();
    
    if (note.isOff()) {
      adsrstate = 3;
      adsrpos = 0;
    } else if (note.isDefined()) {
      // float fscaler = (float)WAVESIZE / 44100.0f;
      freq = note.getFrequency(transpose, detune);
      velocity = _velocity;
      adsrstate = 0;
      adsrpos = 0;
      fphase = 0;
    }
  }

  bool isPlaying() const { return adsrstate < 4 && freq != 0; }

  float getVelocity() const { return velocity; }

  float fphase = 0; // position in input waveform
  float freq = 0; // current frequency
  double phi = 0, phi_mod = 0;

 private:
  float velocity = 1.0f;

  // adsr state
  int adsrstate = 0, adsrpos = 0;
};

#endif
