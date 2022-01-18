#include "FMInstrument.h"

#include "InstrumentVoice.h"
#include "SampleData.h"

#include <cmath>

using namespace std;

class FMInstrumentVoice : public InstrumentVoice {
public:
  FMInstrumentVoice(unsigned int _outSampleRate, const Envelope & amp_envelope, const Envelope & mod_envelope, float _modulation, int _harmonic, int _subharmonic, float _transpose)
    : InstrumentVoice(_outSampleRate, amp_envelope, mod_envelope),
      modulation(_modulation), harmonic(_harmonic), subharmonic(_subharmonic), transpose(_transpose)
  { }

  SampleData render(size_t frames) override {
    float gain0 = 0.5 * decibelsToGain(getGainDB());

    SampleData output(1, frames);
    auto buffer = output.data();

    for (size_t i = 0; i < frames; i++) {
      float gain = gain0 * ampenv.getLevel();
      
      double phi = transpose * getSourceSamplePosition() * 2 * M_PI / getOutSampleRate();
      float s = sinf(phi + modenv.getLevel() * modulation * sinf(phi * harmonic / subharmonic));
      
      stepForward();
      
      buffer[i] += s * gain;

      ampenv.process(1);
      modenv.process(1);
    }

    // applyEffects(output);
	
    return output;
  }

private:
  float modulation, transpose;
  int harmonic, subharmonic;
};

std::unique_ptr<TrackState>
FMInstrument::playNote(float frequency, float velocity, unsigned int outSampleRate, float start_phase) const {
  auto voice = std::make_unique<FMInstrumentVoice>(outSampleRate, getAmpEnvelope(), getModEnvelope(), modulation, harmonic, subharmonic, transpose);
  voice->playNote(frequency, velocity, start_phase);
  return voice;
}

