#ifndef _TESTRANDOM_H_
#define _TESTRANDOM_H_

#include <cstdint>

// A small, fixed-algorithm generator for tests that apply a long run of
// random edits: the same seed gives the same sequence on any platform, which
// the standard engines and distributions do not promise.
struct TestRng {
  explicit TestRng(uint32_t seed) : state(seed != 0 ? seed : 0x9e3779b9u) { }
  uint32_t operator()() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  uint32_t state;
};

#endif
