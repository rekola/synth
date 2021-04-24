#ifndef _INSTRUMENTSTATE_H_
#define _INSTRUMENTSTATE_H_

#include "Note.h"

#include <cmath>

class InstrumentState {
 public:
  InstrumentState() { }

  float getFphase() const { return fphase; }

  float updateADSR(int attack, int decay, float sustain, int release) {
    float adsrvol = 0;
    
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

  static inline float getMidiNoteFrequency(int note) {
    return 440 * powf(2, (note - 69.0) / 12.0);
  }

  void playNote(Note note, int transpose, int detune) {
    int midi_note = note.getMidiNote();
    bool accent = note.hasAccent();
    
    if (midi_note > 1) {
      // float fscaler = (float)WAVESIZE / 44100.0f;
      // freq = getMidiNoteFrequency(note) * fscaler + detune;
      freq = getMidiNoteFrequency(midi_note + transpose + detune / 100.0f);
      acc = accent;
      adsrstate = 0;
      adsrpos = 0;
      fphase = 0;
    } else if (midi_note == 1) {
      adsrstate = 3;
      adsrpos = 0;
    }
  }

  bool hasAccent() const { return acc; }

  float fphase = 0; // position in input waveform
  float freq = 0; // current frequency
  double phi = 0, phi_mod = 0;

 private:
  
  bool acc = false; // has accent

  // adsr state
  int adsrstate = 0, adsrpos = 0;
};

#endif
