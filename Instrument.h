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
  
  int getAttack() const { return a; }
  int getDecay() const { return d; }
  float getSustain() const { return s; }
  int getRelease() const { return r; }
  
  void setDetune(float _detune) { detune = _detune; }
  void setVolume(float _volume) { volume = _volume; }
  void setPan(float _pan) { pan = _pan; }
  void setFilter(float _fcut, float _fres) { fcut = _fcut; fres = _fres; }
  void setFlags(unsigned char _flags) { flags = _flags; }
    
  void setADSR(int _a, int _d, float _s, int _r) {
    a = _a;
    d = _d;
    s = _s;
    r = _r;
  }

  
protected:
  int a = 0, d = 0, r = 0;
  float s = 1.0;
  float detune = 0, volume = 1.0;
  float pan = 0;
  float fcut = 1.0, fres = 0.0;
  unsigned char flags = 0;
};

#endif
