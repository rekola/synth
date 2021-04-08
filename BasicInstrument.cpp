#include "BasicInstrument.h"

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
