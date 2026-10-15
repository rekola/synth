#ifndef _CHANNELBANK_H_
#define _CHANNELBANK_H_

#include "Vec8.h"

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <vector>

// Filters that apply one recurrence to many channels at once, a channel per
// vector lane. A recurrence can't be vectorised along time, but the channels
// of a bus (up to sixteen ambisonic ones plus two sends) are independent and
// share every coefficient, so eight (or four) of them advance in lockstep.
// Each bank keeps its state in lane order, holds one pointer per slot into
// the buffer being filtered (nullptr = no signal this block: zeros go in,
// nothing is written back, and the state keeps evolving - with zero state
// that is exactly a no-op), and processes a block with the samples
// transposed so the channels sit in the lanes.
namespace dsp {

// Most channels a bank is asked to carry (third-order ambisonics).
constexpr int kMaxBankChannels = 16;

// Runs step(x) over frames samples of eight planes: x holds one sample of
// each plane in its lanes, and step returns the processed sample. A null
// plane reads as zero and is never written.
template <class Step>
inline void runPlanes8(float * const * planes, int frames, Step && step) {
  int i = 0;
  v8f rows[8];
  for (; i + kLanes <= frames; i += kLanes) {
    for (int l = 0; l < kLanes; l++) rows[l] = planes[l] ? loadu(planes[l] + i) : splat(0.0f);
    transpose8x8(rows);
    for (int k = 0; k < kLanes; k++) rows[k] = step(rows[k]);
    transpose8x8(rows);
    for (int l = 0; l < kLanes; l++) {
      if (planes[l]) storeu(planes[l] + i, rows[l]);
    }
  }
  for (; i < frames; i++) {
    v8f x = splat(0.0f);
    for (int l = 0; l < kLanes; l++) {
      if (planes[l]) x[l] = planes[l][i];
    }
    x = step(x);
    for (int l = 0; l < kLanes; l++) {
      if (planes[l]) planes[l][i] = x[l];
    }
  }
}

// The same for four planes, with the arithmetic in double.
template <class Step>
inline void runPlanes4(float * const * planes, int frames, Step && step) {
  int i = 0;
  v4d rows[4];
  for (; i + 4 <= frames; i += 4) {
    for (int l = 0; l < 4; l++) {
      v4f in = {};
      if (planes[l]) std::memcpy(&in, planes[l] + i, sizeof(in));
      rows[l] = __builtin_convertvector(in, v4d);
    }
    transpose4x4(rows);
    for (int k = 0; k < 4; k++) rows[k] = step(rows[k]);
    transpose4x4(rows);
    for (int l = 0; l < 4; l++) {
      if (planes[l]) {
        const v4f out = __builtin_convertvector(rows[l], v4f);
        std::memcpy(planes[l] + i, &out, sizeof(out));
      }
    }
  }
  for (; i < frames; i++) {
    v4d x = {};
    for (int l = 0; l < 4; l++) {
      if (planes[l]) x[l] = static_cast<double>(planes[l][i]);
    }
    x = step(x);
    for (int l = 0; l < 4; l++) {
      if (planes[l]) planes[l][i] = static_cast<float>(x[l]);
    }
  }
}

// The slot -> plane table every bank shares, padded to whole lane groups.
class PlaneTable {
 public:
  PlaneTable(int slots, int lanes) : lanes_(lanes), planes_(static_cast<size_t>((slots + lanes - 1) / lanes * lanes), nullptr), active_(planes_.size() / static_cast<size_t>(lanes), 0) { }

  // Set before each apply(); nullptr marks a slot with no signal.
  float *& plane(int slot) { return planes_[static_cast<size_t>(slot)]; }
  int groups() const { return static_cast<int>(active_.size()); }

 protected:
  // True when the group has a signal, or has had one (so its state may be
  // non-zero). A group that never has stays untouched.
  bool wake(int group) {
    if (active_[static_cast<size_t>(group)] == 0) {
      for (int l = 0; l < lanes_; l++) {
        if (planes_[static_cast<size_t>(group * lanes_ + l)]) {
          active_[static_cast<size_t>(group)] = 1;
          break;
        }
      }
    }
    return active_[static_cast<size_t>(group)] != 0;
  }
  float * const * group(int g) const { return &planes_[static_cast<size_t>(g * lanes_)]; }

  int lanes_;
  std::vector<float *> planes_;
  std::vector<unsigned char> active_; // not vector<bool>: plain bytes, no proxy references
};

// Direct-form-II-transposed biquad in double, one shared coefficient set.
class BiquadBank : public PlaneTable {
 public:
  struct Coefficients {
    double a0, a1, a2, b1, b2;
  };

  BiquadBank(int slots, const Coefficients & c) : PlaneTable(slots, 4), c_(c), z_(static_cast<size_t>(groups())) { }

  // Takes effect from the next apply(); the filter's history is kept, so a
  // coefficient change mid-signal doesn't click.
  void setCoefficients(const Coefficients & c) { c_ = c; }
  // Forgets the history (a bank that has sat unused holds stale values).
  void clearState() { std::fill(z_.begin(), z_.end(), State {}); }

  void apply(int frames) {
    const v4d a0 = {c_.a0, c_.a0, c_.a0, c_.a0}, a1 = {c_.a1, c_.a1, c_.a1, c_.a1}, a2 = {c_.a2, c_.a2, c_.a2, c_.a2};
    const v4d b1 = {c_.b1, c_.b1, c_.b1, c_.b1}, b2 = {c_.b2, c_.b2, c_.b2, c_.b2};
    for (int g = 0; g < groups(); g++) {
      if (!wake(g)) continue;
      State & z = z_[static_cast<size_t>(g)];
      runPlanes4(group(g), frames, [&](v4d in) {
        const v4d out = in * a0 + z.z1;
        z.z1 = in * a1 + z.z2 - b1 * out;
        z.z2 = in * a2 - b2 * out;
        return out;
      });
    }
  }

 private:
  struct State {
    v4d z1 = {}, z2 = {};
  };
  Coefficients c_;
  std::vector<State> z_;
};

// Four-pole resonant low-pass, one shared cutoff and resonance per call.
class MoogBank : public PlaneTable {
 public:
  explicit MoogBank(int slots) : PlaneTable(slots, kLanes), s_(static_cast<size_t>(groups())) { }

  void apply(int frames, float fc, float res) {
    if (fc < 0) fc = 0;
    if (fc > 1) fc = 1;
    if (res < 0) res = 0;
    if (res > 4) res = 4;

    float f = fc * 1.16f;
    const float ff = f * f;
    const float fb = res * (1.0f - 0.15f * ff);
    f = 1 - f;

    const v8f vf = splat(f), vfb = splat(fb), gain = splat(0.35013f * ff * ff), k = splat(0.3f);
    for (int g = 0; g < groups(); g++) {
      if (!wake(g)) continue;
      State & s = s_[static_cast<size_t>(g)];
      runPlanes8(group(g), frames, [&](v8f x) {
        x -= s.out4 * vfb;
        x *= gain;
        s.out1 = x + k * s.in1 + vf * s.out1;
        s.in1 = x;
        s.out2 = s.out1 + k * s.in2 + vf * s.out2;
        s.in2 = s.out1;
        s.out3 = s.out2 + k * s.in3 + vf * s.out3;
        s.in3 = s.out2;
        s.out4 = s.out3 + k * s.in4 + vf * s.out4;
        s.in4 = s.out3;
        return s.out4;
      });
    }
  }

 private:
  struct State {
    v8f in1 = {}, in2 = {}, in3 = {}, in4 = {};
    v8f out1 = {}, out2 = {}, out3 = {}, out4 = {};
  };
  std::vector<State> s_;
};

}

#endif
