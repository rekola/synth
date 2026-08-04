#ifndef _HALFBANDFILTER_H_
#define _HALFBANDFILTER_H_

#include <array>
#include <cmath>

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
class HalfbandFilter {
 public:
  HalfbandFilter() { history_.fill(0.0f); }

  // Doubles the sample rate: reads `frames` input samples, writes
  // `2*frames` output samples. Maintains its own delay-line state across
  // calls, so consecutive blocks stitch together seamlessly - safe to
  // call with any per-call frame count.
  void upsample(const float * in, int frames, float * out) {
    for (int i = 0; i < frames; i++) {
      // kInterpolationGain (2x) restores passband amplitude after
      // zero-stuffing - see the class's own design note in
      // plans/drum-bus-saturator.md: the filter's own DC gain is unity,
      // so without this the interpolated signal would sit at half level.
      out[2 * i]     = pushAndConvolve(in[i])   * kInterpolationGain;
      out[2 * i + 1] = pushAndConvolve(0.0f)    * kInterpolationGain;
    }
  }

  // Halves the sample rate: reads `2*frames` input samples, writes
  // `frames` output samples - the same halfband kernel used as an
  // anti-imaging/anti-aliasing lowpass ahead of a fixed decimation phase
  // (every other filtered sample kept, unscaled - no gain compensation
  // needed here, unlike upsample()).
  void downsample(const float * in, int frames, float * out) {
    for (int i = 0; i < frames; i++) {
      pushAndConvolve(in[2 * i]);
      out[i] = pushAndConvolve(in[2 * i + 1]);
    }
  }

 private:
  static constexpr int kTaps = 63; // odd length, center index 31

  static const std::array<float, kTaps> & coefficients() {
    static const std::array<float, kTaps> table = [] {
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
      std::array<float, kTaps> h{};
      // Renormalize so the window's tapering doesn't leave DC gain
      // slightly off unity.
      for (int n = 0; n < kTaps; n++) h[static_cast<size_t>(n)] = static_cast<float>(hd[static_cast<size_t>(n)] / sum);
      return h;
    }();
    return table;
  }

  float pushAndConvolve(float x) {
    history_[static_cast<size_t>(pos_)] = x;
    const auto & coeffs = coefficients();
    float acc = 0.0f;
    for (int k = 0; k < kTaps; k++) {
      int idx = pos_ - k;
      if (idx < 0) idx += kTaps;
      acc += coeffs[static_cast<size_t>(k)] * history_[static_cast<size_t>(idx)];
    }
    pos_++;
    if (pos_ >= kTaps) pos_ = 0;
    return acc;
  }

  static constexpr float kInterpolationGain = 2.0f;

  std::array<float, kTaps> history_{};
  int pos_ = 0;
};

#endif
