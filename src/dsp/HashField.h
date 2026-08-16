#ifndef _HASHFIELD_H_
#define _HASHFIELD_H_

#include <cstdint>

// Stateless, hashed-coordinate value source - the replacement for every
// TreeNode::getRandF()/rand()-based "musical randomization" call site (see
// plans/deterministic-randomness.md for the full audit and migration).
// Given a fixed compile-time salt (one per feature, declared next to the
// code that uses it - the same convention SoundFont.cpp's
// kPercussionJitterSeed and bus/GranularCloud.cpp's kDirectionScatterSeed
// already follow) plus a coordinate (typically a song-position value - see
// NoteCoordinate.h's toHashCoord() for the note-specific packing most call
// sites actually feed this) and a param axis id (paramId() below), it
// returns a value that's a pure function of those three inputs: no stream,
// no draw order, no shared state, so it's safe to call from the audio
// thread, from multiple threads at once, and in any order - the same
// coordinate always produces the same value, and a new param axis can be
// added anywhere without perturbing any existing one.
//
// This is deliberately *not* what audio-rate noise (hiss, dither, per-
// sample grain jitter) should use - those stay on dsp/NoiseGenerator.h's
// small stateful PRNG, one instance per voice/effect-instance, seeded once
// (via HashField, replacing the old getRandF()-drawn seed) and then
// advanced every sample. HashField's job is exactly the "same object, same
// value, every time" case that stateful sequence can't give for free.
class HashField {
 public:
  explicit constexpr HashField(uint64_t salt) noexcept : salt_(salt) { }

  // Core mixing primitive - splitmix64's own finalizer, applied to
  // (coord, param, salt). Every operation here is on a fixed-width
  // unsigned type; the one signed input (`coord`) is cast to unsigned
  // before anything touches it, so there's no implementation-defined
  // signed shift and no platform-dependent behavior anywhere in this
  // function.
  constexpr uint64_t hash64(int64_t coord, uint32_t param) const noexcept {
    uint64_t h = static_cast<uint64_t>(coord) ^ salt_;
    h ^= static_cast<uint64_t>(param) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return h;
  }

  // [0,1) - top 24 bits of the hash as an integer in [0, 2^24), scaled by
  // the exact power-of-two 1/2^24. Both the int->float conversion and the
  // multiply are exact (no rounding): 2^24 is float's full mantissa
  // width, so every value 0..2^24-1 is exactly representable, and
  // multiplying an exactly-representable value by an exact power of two
  // is just an exponent shift, not a rounding operation. The maximum
  // possible result is (2^24-1)/2^24, strictly less than 1.0f - unlike
  // (float)rand()/RAND_MAX, this can never land on exactly 1.0.
  float unit(int64_t coord, uint32_t param) const noexcept {
    uint32_t mantissa = static_cast<uint32_t>(hash64(coord, param) >> 40);
    return static_cast<float>(mantissa) * (1.0f / 16777216.0f);
  }

  // [lo, hi).
  float range(int64_t coord, uint32_t param, float lo, float hi) const noexcept {
    return lo + unit(coord, param) * (hi - lo);
  }

  // [-spread, +spread).
  float bipolar(int64_t coord, uint32_t param, float spread) const noexcept {
    return (unit(coord, param) * 2.0f - 1.0f) * spread;
  }

 private:
  uint64_t salt_;
};

// Compile-time FNV-1a over a string literal - stable across builds/
// compilers (unlike std::hash, which the standard never promises to be),
// used to turn each param axis name into a fixed uint32_t id. All-unsigned
// arithmetic throughout, no undefined behavior.
constexpr uint32_t paramId(const char * name) noexcept {
  uint32_t h = 2166136261u;
  for (const char * p = name; *p; ++p) h = (h ^ static_cast<uint32_t>(*p)) * 16777619u;
  return h;
}

#endif
