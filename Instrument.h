#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Effect.h"
#include "Filter.h"
#include "Note.h"

#include <string>
#include <vector>
#include <cmath>
#include <memory>
#include <deque>

#define MAXDELAYSAMPLES 44100 * 5

// flags
#define DELAYTRACK 0x1

class Instrument {
public:
  explicit Instrument() { }
  virtual ~Instrument() { }

  virtual float getSample() const = 0;

  void setName(const std::string & _name) { name = _name; }
  
  virtual void stepForward() {
    fphase += freq;
  }

  const std::string & getName() const { return name; }
  
  float getDetune() const { return detune; }
  float getVolume() const { return volume; }
  float getPan() const { return pan; }
  unsigned char getFlags() const { return flags; }
  bool getSolo() const { return solo; }
  
  int getAttack() const { return a; }
  int getDecay() const { return d; }
  float getSustain() const { return s; }
  int getRelease() const { return r; }

  void setTranspose(int _transpose) { transpose = _transpose; }
  
  void setDetune(int _detune) { detune = (_detune - 127) / 512.0; }
  void setVolume(float _volume) { volume = _volume; }
  void setPan(float _pan) { pan = _pan; }
  void setFlags(unsigned char _flags) { flags = _flags; }
  void setSolo(bool s) {solo = s; }
    
  void setFilter(float fcut, float fres, bool is_highpass = false) {
    addEffect(std::make_unique<Filter>(fcut, fres, is_highpass));
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

  void playNote(Note note) {
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
  
  void applyEffects(SampleData & data) {
    for (auto & effect : effects) {
      effect->apply(data);
    }
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

  void addEffect(std::unique_ptr<Effect> effect) { effects.push_back(std::move(effect)); }

  void addPendingNote(size_t frame, Note note) {
    pending_notes.push_back(std::pair(frame, note));
  }
  void clearPendingNotes() { pending_notes.clear(); }
  std::deque<std::pair<unsigned int, Note> > & getPendingNotes() { return pending_notes; }
  
protected:
  std::string name;
  
  float freq = 0; // current frequency
  float fphase = 0; // position in input waveform
  bool acc = false; // has accent

  int a = 0, d = 0, r = 0;
  float s = 1.0;
  float detune = 0, volume = 1.0f;
  float pan = 0.5f;
  unsigned char flags = 0;
  short transpose = 0;
  bool solo = false;

  // adsr state
  int adsrstate, adsrpos;

  // delay state
  int delc1, delc2;
  float delaybuf1[MAXDELAYSAMPLES], delaybuf2[MAXDELAYSAMPLES];

  std::vector<std::unique_ptr<Effect> > effects;

  std::deque<std::pair<unsigned int, Note> > pending_notes;
};

#endif
