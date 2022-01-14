#include "Instrument.h"
#include "VoicePool.h"

void
Instrument::playNote(size_t column, float frequency, float velocity, float delay, float detune, VoicePool & voices) const {
  voices.stopVoices(column);
  voices.getVoice(column, *this).playNote(frequency, velocity, delay, detune);
}
