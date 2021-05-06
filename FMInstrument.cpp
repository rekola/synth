#include "FMInstrument.h"

#include <cmath>

// <FM>	Strength of the frequency modulation
// <harmonic>	Harmonic of the modulator (integer)
// <subharmonic>	Subharmonic of the modulator (integer)
// <transpose>	Common note frequency multiplier for carrier and modulator (integer)

static inline float spow(float a, float p) {
  return powf(fabsf(a), p)*(a < 0.0f ? -1.0f : 1.0f);
}

class FMInstrumentVoice : public InstrumentVoice {
public:
  FMInstrumentVoice(int _identifier, const Envelope & amp_envelope, const Envelope & mod_envelope, float _modulation, int _harmonic, int _subharmonic, float _transpose)
    : InstrumentVoice(_identifier, amp_envelope, mod_envelope),
      modulation(_modulation), harmonic(_harmonic), subharmonic(_subharmonic), transpose(_transpose)
  { }

  void render(float * buffer, size_t frames, size_t offset) override {
    float gain = decibelsToGain(getGainDB());
      
    for (size_t i = 0; i < frames; i++) {
      float adsrvol = updateADSR();

      double phi = transpose * getSourceSamplePosition() * 2 * M_PI / 44100.0f;
      float s = sinf(phi + modulation * sinf(phi * harmonic / subharmonic));

      // return s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s;
      // return s;
      
      // * (1 + noise * rand() / RAND_MAX));
      // spow(s, 16);
      
      stepForward();
      
      buffer[i + offset] = s * gain * 0.5f * adsrvol;
    }
  }

private:
  float modulation, transpose;
  int harmonic, subharmonic;
};

std::shared_ptr<InstrumentVoice>
FMInstrument::createVoice(int _identifier) const {
  return std::make_shared<FMInstrumentVoice>(_identifier, getAmpEnvelope(), getModEnvelope(), modulation, harmonic, subharmonic, transpose);
}
