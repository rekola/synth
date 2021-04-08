#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "Instrument.h"

#include <vector>

#define PATTLEN 32
#define MAXDELAYSAMPLES 44100 * 5

// flags
#define DELAYTRACK 0x1
#define HPFILTER 0x2

class Pattern {
 public:
  Pattern() { }

  unsigned char getNote(size_t i) { return i < notes.size() ? notes[i] : 0; }
  void addNote(unsigned char n) { notes.push_back(n); }

  void playNote(unsigned char note_data, float * freqtab, float fscaler) {
    int note = note_data & 0x7f;
    int acct = note_data & 0x80;
    
    if (note > 1) {
      freq = freqtab[note] * fscaler + detune;
      acc = acct;
      adsrstate = 0;
      adsrpos = 0;
      fphase = 0;
    } else if (note == 1) {
      adsrstate = 3;
      adsrpos = 0;
    }
  }

  float updateADSR() {
    float adsrvol = 0;
    
    switch (adsrstate) {
    case 0:
      if (a == 0 || adsrpos >= a) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = 1.0f;
      } else {
	adsrvol = (float)adsrpos / a;
      }
      break;
    case 1:
      if (d == 0 || adsrpos >= d) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = s;
      } else {
	adsrvol = 1.0 - ((1.0 - s) * (float)adsrpos / d);
      }
      break;
    case 2:
      adsrvol = s;
      break;
    case 3:
      if (r == 0 || adsrpos >= r) {
	adsrstate++;
	adsrvol = 0;
      } else {
	adsrvol = s - (s * (float)adsrpos / r);
      }
      break;
    default:
      adsrvol = 0;
      break;
    }
    adsrpos++;

    return adsrvol;
  }
  
  float filtersample(float input) {
    if (!(fcut < 1.0 || fres > 0.0)) return input;
      
    float si = input;
    float f = fcut * 1.16;
    float ff = f * f;
    float fb = fres * (1.0 - 0.15 * ff);
    f = 1 - f;
    
    input -= out4 * fb;
    input *= 0.35013 * ff * ff;
    out1 = input + 0.3 * in1 + f * out1; // Pole 1
    in1  = input;
    out2 = out1 + 0.3 * in2 + f * out2;  // Pole 2
    in2 = out1;
    out3 = out2 + 0.3 * in3 + f * out3;  // Pole 3
    in3  = out2;
    out4 = out3 + 0.3 * in4 + f * out4;  // Pole 4
    in4  = out3;

    char type = flags & HPFILTER;
    
    if (!type) return out4;
    else return si - out4;
  }

  void delaysample(float delaymix1, float delaymix2, float fd1, float delay1, float fd2, float delay2, float *in1, float *in2) {
    float x, y;
    
    x = *in1;
    y = delaybuf1[delc1];
    
    delaybuf1[delc1++] = x + y * fd1;
    if (delc1 >= delay1) delc1 = 0;

    *in1 += delaymix1 * y;
    
    x = *in2;
    y = delaybuf2[delc2];
    
    delaybuf2[delc2++] = x + y * fd2;
    if (delc2 >= delay2) delc2 = 0;
    
    *in2 += delaymix2 * y;
  }

  Instrument instrument;

  int a, d, r;
  float s;
  float vol;
  unsigned char flags;
  float detune;
  float pan;
  float fcut, fres;

  // ?
  float freq, fphase;
  
  // adsr state
  int adsrstate, adsrpos, acc;

private:
  std::vector<unsigned char> notes;

  // filter state
  float in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  float out1 = 0, out2 = 0, out3 = 0, out4 = 0;

  // delay state
  int delc1, delc2;
  float delaybuf1[MAXDELAYSAMPLES], delaybuf2[MAXDELAYSAMPLES];
};

#endif
