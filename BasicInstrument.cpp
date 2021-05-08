#include "BasicInstrument.h"

#include "InstrumentVoice.h"
#include "SampleData.h"

using namespace std;

class BasicInstrumentVoice : public InstrumentVoice {
public:
  BasicInstrumentVoice(unsigned int _outSampleRate, int _identifier, const Envelope & amp_envelope, WaveformType _type)
    : InstrumentVoice(_outSampleRate, _identifier, amp_envelope), type(_type) { }

  SampleData render(size_t frames) override {
    float gain0 = decibelsToGain(getGainDB());

    SampleData output(1, frames);
    auto buffer = output.data();
    
    for (size_t k = 0; k < frames; k++) {
      float gain = gain0 * ampenv.getLevel();

      float i = fmod(getSourceSamplePosition() / getOutSampleRate(), 1.0);
      
      stepForward();

      float s;
      switch (type) {
      case WaveformType::SINE:
	s = sinf(2 * M_PI * i);
	break;
      case WaveformType::SAW:
	s = -1.0 + fmodf(1.0 + 2.0 * i, 2.0);
	break;
      case WaveformType::SQUARE:
	s = i < 0.5 ? -1.0 : 1.0;
	break;
      case WaveformType::NOISE:
	s = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
	break;
      default:
	s = 0.0f;
      }

      buffer[k] += s * gain;
      
      ampenv.process(1);
    }

    applyEffects(output);

    return output;
  }
  
private:
  WaveformType type;
};

std::unique_ptr<InstrumentVoice>
BasicInstrument::createVoice(unsigned int outSampleRate, int identifier) const {
  auto v = std::make_unique<BasicInstrumentVoice>(outSampleRate, identifier, getAmpEnvelope(), type);
  v->createEffectStates(getEffects());
  return v;
}
