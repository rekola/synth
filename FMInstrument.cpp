#include "FMInstrument.h"

#include <stdio.h>   
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <math.h>

// <FM>	Strength of the frequency modulation
// <harmonic>	Harmonic of the modulator (integer)
// <subharmonic>	Subharmonic of the modulator (integer)
// <transpose>	Common note offset for carrier and modulator (integer)
// <a> <d> <s> <r>	Attack, Decay, Sustain, Release

#define POLY 10
#define GAIN 5000.0
#define BUFSIZE 512

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

float
FMInstrument::getSample() const {
  // double sound = GAIN * envelope(&note_active, gate, &env_level, env_time, attack, decay, sustain, release)
  //   * velocity * sin(phi + modulation * sin(phi_mod));
  // env_time += 1.0 / 44100.0;

  return sin(phi + modulation * sin(phi_mod));
}

void
FMInstrument::stepForward() {
  double dphi = M_PI * freq / 22050.0;
  double dphi_mod = dphi * (double)harmonic / (double)subharmonic;
  
  phi += dphi;
  phi_mod += dphi_mod;
  if (phi > 2.0 * M_PI) phi -= 2.0 * M_PI;
  if (phi_mod > 2.0 * M_PI) phi_mod -= 2.0 * M_PI;  
}

      
