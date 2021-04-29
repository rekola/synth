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

class FMInstrumentVoice : public InstrumentVoice {
public:
  FMInstrumentVoice(int _identifier, float _modulation, int _harmonic, int _subharmonic)
    : InstrumentVoice(_identifier),
      modulation(_modulation), harmonic(_harmonic), subharmonic(_subharmonic)
  { }

  void render(float * buffer, size_t frames) override {
    for (size_t i = 0; i < frames; i++) {
      // double sound = GAIN * envelope(&note_active, gate, &env_level, env_time, attack, decay, sustain, release)
      //   * velocity * sin(phi + modulation * sin(phi_mod));
      // env_time += 1.0 / 44100.0;

      float phi = getWavePosition() * 2 * M_PI / 22050.0f;
      float s = sinf(phi + modulation * sinf(phi * harmonic / subharmonic));
      // return s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s;
      // return s;
      
      // * (1 + noise * rand() / RAND_MAX));
      
#if 0
      double dphi = M_PI * freq / 22050.0;
      double dphi_mod = dphi * (double)harmonic / (double)subharmonic;
      
      phi += dphi;
      phi_mod += dphi_mod;
      
      if (phi > 2.0 * M_PI) phi -= 2.0 * M_PI;
      if (phi_mod > 2.0 * M_PI) phi_mod -= 2.0 * M_PI;
#else
      stepForward();
#endif
      // spow(s, 16);
      
      buffer[i] = s * getVelocity() * 0.5f;
    }
  }

private:
  float modulation;
  // velocity, attack, decay, sustain, release, env_time, env_level;
  int harmonic, subharmonic;
  // double phi = 0, phi_mod = 0;
};

std::shared_ptr<InstrumentVoice>
FMInstrument::createVoice(int _identifier) const {
  return std::make_shared<FMInstrumentVoice>(_identifier, modulation, harmonic, subharmonic);
}
