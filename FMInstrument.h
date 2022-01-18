#ifndef _FMINSTRUMENT_H_
#define _FMINSTRUMENT_H_

#include "Instrument.h"

// modulation:	Strength of the frequency modulation
// harmonic:	Harmonic of the modulator
// subharmonic:	Subharmonic of the modulator
// transpose:	Common frequency scale for carrier and modulator

class FMInstrument : public Instrument {
public:
  explicit FMInstrument(float _modulation, int _harmonic, int _subharmonic, Envelope _ampenv, Envelope _modenv, float _transpose = 1.0f)
    : Instrument(1, _ampenv, _modenv), modulation(_modulation), harmonic(_harmonic), subharmonic(_subharmonic), transpose(_transpose) { }

  std::unique_ptr<TrackState> playNote(float frequency, float velocity, unsigned int outSampleRate, float start_phase) const override;

private:
  float modulation;
  int harmonic, subharmonic;
  float transpose;
};

#endif
