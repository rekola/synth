#ifndef _NOISEGENERATOR_H_
#define _NOISEGENERATOR_H_

#include <cstdint>

// Fast, deterministic, full 32-bit-resolution PRNG (xorshift32) for
// audio-rate noise generation - deliberately not HashField (dsp/HashField.h):
// HashField answers "what's the value for this coordinate," recomputed
// fresh each call, which is exactly wrong for a signal that needs a new,
// unrelated sample every single audio-rate tick (potentially several times
// per sample) rather than one fixed value per note. Each instance is
// seeded once per voice (via HashField, from that voice's own
// NoteCoordinate - the same one-time-per-note cost as InstrumentVoice's
// own start-phase derivation), so several simultaneously-active voices
// drawing from their own instance (e.g. a chord of NoiseVoice notes,
// Noise.cpp - each owns exactly one NoiseGenerator via its own
// NoiseStream, not one per channel) stay decorrelated from each other
// rather than reading the same sequence in lockstep.
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
