#include "BasicInstrument.h"

#if 0
bool BasicInstrument::is_initialized = false;
float BasicInstrument::waves[4][WAVESIZE];

void
BasicInstrument::initialize() {
  is_initialized = true;

  for (int i = 0; i < WAVESIZE; i++) {
    waves[int(WaveformType::SINE)][i] = sinf(i * 2.0 * M_PI / (float)WAVESIZE);
    waves[int(WaveformType::SAW)][i] = -1.0 + fmod(1.0 + 2.0 * i / (float)WAVESIZE, 2.0);
    waves[int(WaveformType::SQUARE)][i] = (i < WAVESIZE / 2) ? -1.0 : 1.0;
    waves[int(WaveformType::NOISE)][i] = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
  }
}
#endif

class BasicInstrumentVoice : public InstrumentVoice {
public:
  BasicInstrumentVoice(int _identifier, const Envelope & amp_envelope, WaveformType _type) : InstrumentVoice(_identifier, amp_envelope), type(_type) { }

  void render(float * buffer, size_t frames, size_t offset) override {
    float gain = decibelsToGain(getGainDB());

    for (size_t k = 0; k < frames; k++) {
      float adsrvol = updateADSR();

      float i = fmod(getSourceSamplePosition() / 44100.0f, 1.0);
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

      buffer[k + offset] = s * gain * adsrvol;
    }
  }
  
private:
  WaveformType type;
};

std::shared_ptr<InstrumentVoice>
BasicInstrument::createVoice(int _identifier) const {
  return std::make_shared<BasicInstrumentVoice>(_identifier, getAmpEnvelope(), type);
}
