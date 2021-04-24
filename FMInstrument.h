#ifndef _FMINSTRUMENT_H_
#define _FMINSTRUMENT_H_

#include "Instrument.h"

// <FM>	Strength of the frequency modulation
// <harmonic>	Harmonic of the modulator (integer)
// <subharmonic>	Subharmonic of the modulator (integer)
// <transpose>	Common note offset for carrier and modulator (integer)
// <a> <d> <s> <r>	Attack, Decay, Sustain, Release

// Harpsichord 7.8 3 5 24 0.01 0.8 0.0 0.1
// Bell 3.5 7 9 0 0.01 0.2 0.3 1.5
// Oboe 0.7 1 3 24 0.05 0.3 0.8 0.2

class FMInstrument : public Instrument {
public:
  explicit FMInstrument(float _modulation, int _harmonic, int _subharmonic, float _n = 2)
    : modulation(_modulation), harmonic(_harmonic), subharmonic(_subharmonic), noise(0), n(_n) { }

  float getSample(const InstrumentState & state) const override;
  void stepForward(InstrumentState & state) override;
  
private:
  double modulation, velocity, attack, decay, sustain, release, env_time, env_level;
  int harmonic, subharmonic, transpose, note, gate, note_active, rate;
  float noise, n;
};

#endif
