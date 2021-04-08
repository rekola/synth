#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

// flags
#define DELAYTRACK 0x1
#define HPFILTER 0x2

class Instrument {
public:
  Instrument() { }
  virtual ~Instrument() { }

  virtual float getSample(float fphase) const = 0;

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
  
protected:
  int a = 0, d = 0, r = 0;
  float s = 1.0;
  float detune = 0, volume = 1.0f;
  float pan = 0.5f;
  float fcut = 1.0, fres = 0.0;
  unsigned char flags = 0;
  bool solo = false;
};

#endif
