#include "FMInstrument.h"

#include <cmath>

// <FM>	Strength of the frequency modulation
// <harmonic>	Harmonic of the modulator (integer)
// <subharmonic>	Subharmonic of the modulator (integer)
// <transpose>	Common note offset for carrier and modulator (integer)
// <a> <d> <s> <r>	Attack, Decay, Sustain, Release

static inline double envelope(int * note_active, int gate, double *env_level, double t, double attack, double decay, double sustain, double release) {
  if (gate)  {
    if (t > attack + decay) return (*env_level = sustain);
    if (t > attack) return (*env_level = 1.0 - (1.0 - sustain) * (t - attack) / decay);
    return (*env_level = t / attack);
  } else {
    if (t > release) {
      if (note_active) *note_active = 0;
      return (*env_level = 0);
    }
    return (*env_level * (1.0 - t / release));
  }
}

static inline float spow(float a, float p) {
  return powf(fabsf(a), p)*(a < 0.0f ? -1.0f : 1.0f);
}

float
FMInstrument::getSample(const InstrumentState & state) const {
  // double sound = GAIN * envelope(&note_active, gate, &env_level, env_time, attack, decay, sustain, release)
  //   * velocity * sin(phi + modulation * sin(phi_mod));
  // env_time += 1.0 / 44100.0;

  float s = sin(state.phi + modulation * sin(state.phi_mod));
  // return s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s;
  // return s;
  return s; // spow(s, 16);
  
  // * (1 + noise * rand() / RAND_MAX));
}

void
FMInstrument::stepForward(InstrumentState & state) {
  Instrument::stepForward(state);

  double dphi = M_PI * state.freq / 22050.0;
  double dphi_mod = dphi * (double)harmonic / (double)subharmonic;
    
  state.phi += dphi;
  state.phi_mod += dphi_mod;
  
  if (state.phi > 2.0 * M_PI) state.phi -= 2.0 * M_PI;
  if (state.phi_mod > 2.0 * M_PI) state.phi_mod -= 2.0 * M_PI;  
}
