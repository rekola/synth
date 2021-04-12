#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include <vector>
#include <cmath>

#define WAVESIZE 1024
#define MAXDELAYSAMPLES 44100 * 5

// flags
#define DELAYTRACK 0x1
#define HPFILTER 0x2

class Instrument {
public:
  explicit Instrument() { }
  virtual ~Instrument() { }

  virtual float getSample() const = 0;

  void stepForward() {
    fphase += freq;
  }
  
  float getDetune() const { return detune; }
  float getVolume() const { return volume; }
  float getPan() const { return pan; }
  float getFcut() const { return fcut; }
  float getFres() const { return fres; }
  unsigned char getFlags() const { return flags; }
  bool getSolo() const { return solo; }
  
  int getAttack() const { return a; }
  int getDecay() const { return d; }
  float getSustain() const { return s; }
  int getRelease() const { return r; }
  
  void setDetune(int _detune) { detune = (_detune - 127) / 512.0; }
  void setVolume(int _volume) { volume = _volume / 128.0f; }
  void setPan(int _pan) { pan = _pan / 255.0; }
  void setFlags(unsigned char _flags) { flags = _flags; }
  void setSolo(bool s) {solo = s; }
    
  void setFilter(int _fcut, int _fres) {
    fcut = _fcut / 255.0f;
    fres = _fres / 63.0f;
  }

  void setADSR(int _a, int _d, int _s, int _r) {
    a = _a * 44100 * 5 / 255;
    d = _d * 44100 * 5 / 255;
    s = _s / 255.0f;
    r = _r * 44100 * 5 / 255;
  }

  float updateADSR() {
    float adsrvol = 0;
    
    switch (adsrstate) {
    case 0:
      if (getAttack() == 0 || adsrpos >= getAttack()) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = 1.0f;
      } else {
	adsrvol = (float)adsrpos / getAttack();
      }
      break;
    case 1:
      if (getDecay() == 0 || adsrpos >= getDecay()) {
	adsrstate++;
	adsrpos = 0;
	adsrvol = getSustain();
      } else {
	adsrvol = 1.0 - ((1.0 - getSustain()) * (float)adsrpos / getDecay());
      }
      break;
    case 2:
      adsrvol = getSustain();
      break;
    case 3:
      if (getRelease() == 0 || adsrpos >= getRelease()) {
	adsrstate++;
	adsrvol = 0;
      } else {
	adsrvol = getSustain() - (getSustain() * (float)adsrpos / getRelease());
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

  void playNote(unsigned char note_data) {
    int note = note_data & 0x7f;
    int acct = note_data & 0x80;
    
    if (note > 1) {
      float fscaler = (float)WAVESIZE / 44100.0f;
      freq = getMidiNoteFrequency(note) * fscaler + detune;
      acc = acct;
      adsrstate = 0;
      adsrpos = 0;
      fphase = 0;
    } else if (note == 1) {
      adsrstate = 3;
      adsrpos = 0;
    }
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

  float getFphase() const { return fphase; }
  bool hasAccent() const { return acc; }
  
protected:
  float freq = 0; // current frequency
  float fphase = 0; // position in input waveform
  bool acc = false; // has accent

  int a = 0, d = 0, r = 0;
  float s = 1.0;
  float detune = 0, volume = 1.0f;
  float pan = 0.5f;
  float fcut = 1.0, fres = 0.0;
  unsigned char flags = 0;
  bool solo = false;

  // adsr state
  int adsrstate, adsrpos;

    // filter state
  float in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  float out1 = 0, out2 = 0, out3 = 0, out4 = 0;

  // delay state
  int delc1, delc2;
  float delaybuf1[MAXDELAYSAMPLES], delaybuf2[MAXDELAYSAMPLES];

};

#endif
