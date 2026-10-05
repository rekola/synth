#ifndef _HALFBANDFILTER_H_
#define _HALFBANDFILTER_H_

#include "Vec8.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

// Fixed-coefficient, linear-phase halfband FIR lowpass, used in cascade
// (two instances for 4x, three for 8x) to build the oversampling stages
// the bus saturator's waveshaper needs (see
// plans/drum-bus-saturator.md) - a halfband filter's cutoff sits exactly
// at 1/4 of the *upsampled* rate (i.e. Nyquist of the lower rate), the
// standard, cheapest design point for a 2x resampler: every even-indexed
// coefficient (relative to the center tap) works out to exactly zero,
// which halfbandCoefficients() below computes directly rather than
// relying on sin() landing on an incidental near-zero.
//
// Coefficients are a windowed-sinc design (4-term Blackman-Harris window
// - ~92dB theoretical sidelobe level, plenty for oversampling a signal
// that's already been band-limited by the saturator's own pre-distortion
// bandpass; this doesn't need to be a mastering-grade resampler), built
// once into a shared, process-wide static table on first use (the design
// has no parameters, so every instance reads the same table) rather than
// per instance - only the delay-line state below is per-instance.
//
// Evaluated as a polyphase filter over whole blocks. Of the 63 taps only the
// centre and the 32 at even indices are non-zero, so a zero-stuffed
// upsample is one 32-tap FIR on the input (the even outputs) plus a single
// delayed copy (the odd outputs), and a decimating downsample is the same
// FIR on the odd input samples plus the delayed even ones - about a fifth of
// the multiplies of running all 63 taps on every sample, in a layout that
// vectorises eight outputs at a time.
class HalfbandFilter {
 public:
  // Doubles the sample rate: reads `frames` input samples, writes
  // `2*frames` output samples. Maintains its own delay-line state across
  // calls, so consecutive blocks stitch together seamlessly - safe to
  // call with any per-call frame count.
  void upsample(const float * in, int frames, float * out) {
    if (frames <= 0) return;
    const auto & t = taps();
    const size_t n = static_cast<size_t>(frames);
    const size_t padded = paddedFor(n);

    // [31 samples of history | this block | zero padding].
    x_.resize(kHistory + padded);
    std::memcpy(x_.data(), up_history_.data(), kHistory * sizeof(float));
    std::memcpy(x_.data() + kHistory, in, n * sizeof(float));
    std::fill(x_.begin() + static_cast<long>(kHistory + n), x_.end(), 0.0f);

    even_.resize(padded);
    fir(x_.data() + kHistory, padded, t.up, even_.data());

    // kInterpolationGain (2x) restores passband amplitude after
    // zero-stuffing: the filter's own DC gain is unity, so without it the
    // interpolated signal would sit at half level. It is folded into the
    // taps (an exact power-of-two scaling). The odd outputs are the centre
    // tap alone: the input delayed by 15.
    const float center = t.center * kInterpolationGain;
    const float * delayed = x_.data() + kHistory - kCenterDelay;
    mixed_.resize(2 * padded);
    for (size_t i = 0; i < padded; i += dsp::kLanes) {
      const dsp::v8f e = dsp::loadu(even_.data() + i);
      const dsp::v8f o = dsp::splat(center) * dsp::loadu(delayed + i);
      dsp::storeu(mixed_.data() + 2 * i, __builtin_shufflevector(e, o, 0, 8, 1, 9, 2, 10, 3, 11));
      dsp::storeu(mixed_.data() + 2 * i + dsp::kLanes, __builtin_shufflevector(e, o, 4, 12, 5, 13, 6, 14, 7, 15));
    }
    std::memcpy(out, mixed_.data(), 2 * n * sizeof(float));

    std::memcpy(up_history_.data(), x_.data() + n, kHistory * sizeof(float));
  }

  // Halves the sample rate: reads `2*frames` input samples, writes
  // `frames` output samples - the same halfband kernel used as an
  // anti-imaging/anti-aliasing lowpass ahead of a fixed decimation phase
  // (every other filtered sample kept, unscaled - no gain compensation
  // needed here, unlike upsample()).
  void downsample(const float * in, int frames, float * out) {
    if (frames <= 0) return;
    const auto & t = taps();
    const size_t n = static_cast<size_t>(frames);
    const size_t padded = paddedFor(n);

    // Odd input samples carry the FIR; even ones, delayed by 15, the
    // centre tap. Each is [history | this block | zero padding].
    x_.resize(kHistory + padded);
    even_.resize(kCenterDelay + padded);
    std::memcpy(x_.data(), down_odd_history_.data(), kHistory * sizeof(float));
    std::memcpy(even_.data(), down_even_history_.data(), kCenterDelay * sizeof(float));
    for (size_t i = 0; i < n; i++) {
      even_[kCenterDelay + i] = in[2 * i];
      x_[kHistory + i] = in[2 * i + 1];
    }
    std::fill(x_.begin() + static_cast<long>(kHistory + n), x_.end(), 0.0f);
    std::fill(even_.begin() + static_cast<long>(kCenterDelay + n), even_.end(), 0.0f);

    acc_.resize(padded);
    fir(x_.data() + kHistory, padded, t.down, acc_.data());
    const dsp::v8f center = dsp::splat(t.center);
    for (size_t i = 0; i < padded; i += dsp::kLanes) {
      dsp::storeu(acc_.data() + i, dsp::loadu(acc_.data() + i) + center * dsp::loadu(even_.data() + i));
    }
    std::memcpy(out, acc_.data(), n * sizeof(float));

    std::memcpy(down_odd_history_.data(), x_.data() + n, kHistory * sizeof(float));
    std::memcpy(down_even_history_.data(), even_.data() + n, kCenterDelay * sizeof(float));
  }

 private:
  static constexpr int kTaps = 63; // odd length, center index 31
  static constexpr size_t kPhaseTaps = 32; // the non-zero taps at even indices
  static constexpr size_t kHistory = kPhaseTaps - 1;
  static constexpr size_t kCenterDelay = (kTaps - 1) / 2 / 2; // 15 samples at the lower rate
  static constexpr float kInterpolationGain = 2.0f;

  struct Taps {
    std::array<float, kPhaseTaps> up{};   // h[2p] * kInterpolationGain
    std::array<float, kPhaseTaps> down{}; // h[2p]
    float center = 0.0f;                  // h[31]
  };

  static size_t paddedFor(size_t n) { return (n + dsp::kLanes - 1) / dsp::kLanes * dsp::kLanes; }

  // out[i] = sum over p of c[p] * x[i - p], for i in [0, n), n a multiple
  // of 8; x[-31 .. -1] must be readable (the history).
  static void fir(const float * x, size_t n, const std::array<float, kPhaseTaps> & c, float * out) {
    for (size_t i = 0; i < n; i += dsp::kLanes) {
      dsp::v8f acc = dsp::splat(0.0f);
      for (size_t p = 0; p < kPhaseTaps; p++) acc += dsp::splat(c[p]) * dsp::loadu(x + i - p);
      dsp::storeu(out + i, acc);
    }
  }

  static const Taps & taps() {
    static const Taps table = [] {
      std::array<double, kTaps> hd{};
      constexpr int kCenter = (kTaps - 1) / 2;
      constexpr double kCutoff = 0.25; // quarter of the upsampled rate == halfband
      double sum = 0.0;
      for (int n = 0; n < kTaps; n++) {
        int j = n - kCenter;
        double ideal;
        if (j == 0) {
          ideal = 2.0 * kCutoff;
        } else if (j % 2 == 0) {
          ideal = 0.0; // exact halfband zero - see class comment
        } else {
          double x = M_PI * 2.0 * kCutoff * static_cast<double>(j);
          ideal = std::sin(x) / (M_PI * static_cast<double>(j));
        }
        // 4-term Blackman-Harris window.
        constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
        double phase = 2.0 * M_PI * static_cast<double>(n) / static_cast<double>(kTaps - 1);
        double w = a0 - a1 * std::cos(phase) + a2 * std::cos(2.0 * phase) - a3 * std::cos(3.0 * phase);
        hd[static_cast<size_t>(n)] = ideal * w;
        sum += hd[static_cast<size_t>(n)];
      }
      // Renormalize so the window's tapering doesn't leave DC gain
      // slightly off unity.
      Taps t;
      for (size_t p = 0; p < kPhaseTaps; p++) {
        const float h = static_cast<float>(hd[2 * p] / sum);
        t.down[p] = h;
        t.up[p] = h * kInterpolationGain;
      }
      t.center = static_cast<float>(hd[kCenter] / sum);
      return t;
    }();
    return table;
  }

  // The last 31 samples fed to each direction's FIR, and the last 15 of the
  // even-phase stream the downsampler delays.
  std::array<float, kHistory> up_history_{};
  std::array<float, kHistory> down_odd_history_{};
  std::array<float, kCenterDelay> down_even_history_{};

  // Block scratch, grown on first use and reused.
  std::vector<float> x_, even_, acc_, mixed_;
};

#endif
