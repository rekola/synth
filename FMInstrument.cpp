
#include "FMInstrument.h"

#include "InstrumentVoice.h"

#include <cmath>

using namespace std;

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

  SampleData render(size_t frames) override {
    float gain0 = 0.5 * decibelsToGain(getGainDB());

    SampleData output(1, frames);
    auto buffer = output.data();

    for (size_t i = 0; i < frames; i++) {
      float gain = gain0 * ampenv.getLevel();
      
      double phi = transpose * getSourceSamplePosition() * 2 * M_PI / 44100.0f;
      float s = sinf(phi + modenv.getLevel() * modulation * sinf(phi * harmonic / subharmonic));

      // return s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s*s;
      // return s;
      
      // * (1 + noise * rand() / RAND_MAX));
      // spow(s, 16);
      
      stepForward();
      
      buffer[i] += s * gain;

      ampenv.process(1);
      modenv.process(1);
    }

    applyEffects(output);
	
    return output;
  }

private:
  float modulation, transpose;
  int harmonic, subharmonic;
};

std::unique_ptr<InstrumentVoice>
FMInstrument::createVoice(int _identifier) const {
  return std::make_unique<FMInstrumentVoice>(_identifier, getAmpEnvelope(), getModEnvelope(), modulation, harmonic, subharmonic, transpose);
}
