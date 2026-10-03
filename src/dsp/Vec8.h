#ifndef _VEC8_H_
#define _VEC8_H_

// Eight floats as one compiler vector (compiled to SSE/AVX/NEON as the
// target allows).
namespace dsp {

constexpr int kLanes = 8;

typedef float v8f __attribute__((vector_size(32)));

inline v8f splat(float x) { return v8f{ x, x, x, x, x, x, x, x }; }

}

#endif
