#ifndef _ENVELOPE_H_
#define _ENVELOPE_H_

class Envelope {
 public:
  Envelope() : a(0), d(0), s(1.0f), r(0) { }
 Envelope(int _a, int _d, float _s, int _r)
   : a(_a * 44100 * 5 / 255), d(_d * 44100 * 5 / 255), s(_s), r(_r * 44100 * 5 / 255)
    {
      
    }

  int getAttack() const { return a; }
  int getDecay() const { return d; }
  float getSustain() const { return s; }
  int getRelease() const { return r; }

 private:
  int a, d;
  float s;
  int r;
};

#endif
