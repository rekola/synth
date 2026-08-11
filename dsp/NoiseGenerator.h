#ifndef _NOISEGENERATOR_H_
#define _NOISEGENERATOR_H_

#include <cstdint>

// Fast, deterministic, full 32-bit-resolution PRNG (xorshift32) for
// audio-rate noise generation - deliberately NOT VoiceState::getRandF()
// (backed by libc rand()): some platforms only guarantee 15-bit RAND_MAX
// resolution, and drawing from rand() at audio rate (potentially several
// times per sample) would perturb the same shared sequence SongState uses
// for musical randomization (velocity/delay, NoteMultiplier's phase
// spread) at a rate entirely unrelated to note events. Each instance is
// seeded once per voice (from getRandF() - the same one-time-per-note cost
// as the existing start-phase randomization in InstrumentTrackState.h), so
// multiple simultaneous noise streams (e.g. NoiseVoice's independent left/
// right/sendA/sendB draws) stay decorrelated from each other.
class NoiseGenerator {
 public:
  explicit NoiseGenerator(uint32_t seed) : state_(seed != 0 ? seed : 0x9e3779b9u) { }

  // Uniform sample in [-1, 1).
  float next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);
  }

 private:
  uint32_t state_;
};

#endif
