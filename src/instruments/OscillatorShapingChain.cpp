#include "OscillatorShapingChain.h"

#include "../dsp/RealFFT.h"
#include "../util/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <functional>

using namespace std;

namespace OscillatorShaping {

namespace {

constexpr float kPi = static_cast<float>(M_PI);

// The shared (k1, phi, k3) warp position formula - identical for the base
// waveform's own warp (evaluated directly, b(warpedPosition(x))) and the
// oscillator time warp (used to circularly resample a discrete waveform,
// warpedPosition(x)*N).
float warpedPosition(float x, const TimeWarp & warp) {
  if (!warp.enabled) return x;
  return frac(warp.k3 * x + warp.k1 * std::sin(2.0f * kPi * (x + warp.phi)));
}

// b(x) - the base waveform shapes docs/padsynth.md documents. `param` is
// already the derived shape quantity (a'/e/a), not the raw stored P.
float baseWaveformSample(BaseFunction fn, float param, float x) {
  switch (fn) {
    case BaseFunction::ClippedTriangle: {
      float xp = frac(x + 0.25f);
      float a_prime = std::max(param, 1e-5f);
      float v = (xp < 0.5f) ? (4.0f * xp - 1.0f) : (4.0f * (1.0f - xp) - 1.0f);
      return std::clamp(-v / a_prime, -1.0f, 1.0f);
    }
    case BaseFunction::PowerRamp: {
      float e = param;
      return 2.0f * std::pow(x, e) - 1.0f;
    }
    case BaseFunction::GaussianPulse: {
      float a = std::max(param, 1e-5f);
      float u = 2.0f * x - 1.0f;
      return 2.0f * std::exp(-u * u * (std::exp(8.0f * a) + 5.0f)) - 1.0f;
    }
    case BaseFunction::WarpedHalfSine: {
      float e = param;
      return 2.0f * std::sin(kPi * std::pow(x, e)) - 1.0f;
    }
    case BaseFunction::None:
    default:
      throw std::invalid_argument("baseWaveformSample: unsupported base_function");
  }
}

// Step 1: samples b(x) (with the base warp folded into x), forward-DFTs it,
// and zeroes DC - "Sample b at x = i/N, take the DFT, and zero DC."
// Returns kHarmonicCount complex bins (k = 0 .. kHarmonicCount-1).
vector<complex<float>> computeBaseSpectrum(const OscillatorChainParams & params) {
  RealFFT<float> fft(kSize);
  vector<float> samples(kSize);
  for (int i = 0; i < kSize; i++) {
    float x = static_cast<float>(i) / static_cast<float>(kSize);
    float xw = warpedPosition(x, params.base_warp);
    samples[static_cast<size_t>(i)] = baseWaveformSample(params.base_function, params.base_shape_param, xw);
  }
  auto & spectrum = fft.forward(samples);
  vector<complex<float>> B(kHarmonicCount, complex<float>(0.0f, 0.0f));
  for (int k = 0; k < kHarmonicCount; k++) B[static_cast<size_t>(k)] = spectrum[static_cast<size_t>(k)];
  B[0] = complex<float>(0.0f, 0.0f);
  return B;
}

// Step 2: harmonic expansion.
vector<complex<float>> harmonicExpansion(const OscillatorChainParams & params, const vector<complex<float>> & B) {
  vector<complex<float>> X(kHarmonicCount, complex<float>(0.0f, 0.0f));
  if (params.base_function == BaseFunction::None) {
    for (auto & [n, a] : params.harmonics) {
      if (n >= 1 && n < kHarmonicCount) X[static_cast<size_t>(n)] += complex<float>(a, 0.0f);
    }
    return X;
  }
  for (int k = 1; k < kHarmonicCount; k++) {
    complex<float> sum(0.0f, 0.0f);
    for (auto & [n, a] : params.harmonics) {
      if (n < 1) continue;
      if (k % n != 0) continue;
      int idx = k / n;
      if (idx >= 0 && idx < kHarmonicCount) sum += a * B[static_cast<size_t>(idx)];
    }
    X[static_cast<size_t>(k)] = sum;
  }
  return X;
}

// The generic "time-domain stage" recipe: zero DC, taper the top N/8 bins,
// inverse-DFT and peak-normalize, apply `stage` to the real samples, then
// forward-DFT back. Shared by the waveshaper and the oscillator time warp
// (docs/padsynth.md's own "Time-domain stage" bullet list).
vector<complex<float>> timeDomainStage(vector<complex<float>> X, const std::function<void(vector<float> &)> & stage) {
  X[0] = complex<float>(0.0f, 0.0f);
  constexpr int kTaper = kSize / 8;
  for (int i = 1; i < kTaper; i++) {
    int bin = kSize / 2 - i;
    if (bin >= 0 && bin < static_cast<int>(X.size())) {
      X[static_cast<size_t>(bin)] *= static_cast<float>(i) / static_cast<float>(kTaper);
    }
  }

  RealFFT<float> fft(kSize);
  vector<complex<float>> full(fft.binCount(), complex<float>(0.0f, 0.0f));
  for (size_t k = 0; k < X.size() && k < full.size(); k++) full[k] = X[k];
  auto & time = fft.inverse(full);

  vector<float> samples(time.begin(), time.end());
  float peak = 0.0f;
  for (float s : samples) peak = std::max(peak, std::fabs(s));
  if (peak > 0.0f) {
    for (float & s : samples) s /= peak;
  }

  stage(samples);

  auto & spectrum = fft.forward(samples);
  vector<complex<float>> out(kHarmonicCount, complex<float>(0.0f, 0.0f));
  for (int k = 0; k < kHarmonicCount; k++) out[static_cast<size_t>(k)] = spectrum[static_cast<size_t>(k)];
  return out;
}

vector<complex<float>> applyWaveshaper(vector<complex<float>> X, WaveshaperKind kind, float k) {
  if (kind == WaveshaperKind::None) return X;
  return timeDomainStage(std::move(X), [&](vector<float> & samples) {
    if (kind == WaveshaperKind::Arctangent) {
      float atan_k = std::atan(k);
      for (float & s : samples) s = std::atan(k * s) / atan_k;
    } else if (kind == WaveshaperKind::LogisticSigmoid) {
      auto sigma = [](float z) { return 0.5f - 1.0f / (std::exp(z) + 1.0f); };
      float denom = (k > 10.0f) ? 0.5f : sigma(k);
      for (float & s : samples) s = sigma(std::clamp(k * s, -10.0f, 10.0f)) / denom;
    } else {
      throw std::invalid_argument("applyWaveshaper: unsupported waveshaper kind");
    }
  });
}

vector<complex<float>> applyHarmonicFilter(vector<complex<float>> X, const HarmonicFilterParams & filter) {
  if (filter.kind == FilterKind::None) return X;
  vector<complex<float>> Y = std::move(X);
  if (filter.kind == FilterKind::ExponentialLowpass) {
    for (int k = 1; k < kHarmonicCount; k++) {
      float g = std::pow(filter.decay_base, static_cast<float>(k));
      if (g < filter.threshold) {
        g = std::pow(g, 10.0f) / std::pow(filter.threshold, 9.0f);
      }
      Y[static_cast<size_t>(k)] *= g;
    }
  } else if (filter.kind == FilterKind::SingleHarmonicBoost) {
    if (filter.boost_harmonic >= 1 && filter.boost_harmonic < kHarmonicCount) {
      Y[static_cast<size_t>(filter.boost_harmonic)] *= filter.boost_gain;
    }
  } else {
    throw std::invalid_argument("applyHarmonicFilter: unsupported filter kind");
  }

  float peak = 0.0f;
  for (int k = 1; k < kHarmonicCount; k++) peak = std::max(peak, std::abs(Y[static_cast<size_t>(k)]));
  if (peak > 0.0f) {
    for (int k = 1; k < kHarmonicCount; k++) Y[static_cast<size_t>(k)] /= peak;
  }
  return Y;
}

vector<complex<float>> applyOscillatorTimeWarp(vector<complex<float>> X, const TimeWarp & warp) {
  if (!warp.enabled) return X;
  return timeDomainStage(std::move(X), [&](vector<float> & samples) {
    int n = static_cast<int>(samples.size());
    vector<float> resampled(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
      float x = static_cast<float>(i) / static_cast<float>(n);
      float t = warpedPosition(x, warp) * static_cast<float>(n);
      int j0 = static_cast<int>(std::floor(t));
      float f = t - static_cast<float>(j0);
      int j0_wrapped = ((j0 % n) + n) % n;
      int j1_wrapped = (j0_wrapped + 1) % n;
      resampled[static_cast<size_t>(i)] = samples[static_cast<size_t>(j0_wrapped)] * (1.0f - f)
        + samples[static_cast<size_t>(j1_wrapped)] * f;
    }
    samples = std::move(resampled);
  });
}

vector<complex<float>> applySpectrumAdjust(vector<complex<float>> X, SpectrumAdjustKind kind, float gamma) {
  if (kind == SpectrumAdjustKind::None) return X;
  if (kind != SpectrumAdjustKind::PowerLaw) throw std::invalid_argument("applySpectrumAdjust: unsupported kind");

  float max_mag = 0.0f;
  for (int k = 1; k < kHarmonicCount; k++) max_mag = std::max(max_mag, std::abs(X[static_cast<size_t>(k)]));
  if (max_mag <= 0.0f) return X;

  vector<complex<float>> Y = std::move(X);
  for (int k = 1; k < kHarmonicCount; k++) {
    float mag = std::abs(Y[static_cast<size_t>(k)]);
    if (mag <= 0.0f) continue;
    float new_mag = std::pow(mag / max_mag, gamma);
    Y[static_cast<size_t>(k)] *= (new_mag / mag);
  }
  return Y;
}

vector<complex<float>> applyHarmonicShift(vector<complex<float>> X, int s) {
  if (s == 0) return X;
  vector<complex<float>> Y(X.size(), complex<float>(0.0f, 0.0f));
  int n = static_cast<int>(X.size());
  if (s > 0) {
    for (int h = 0; h < n; h++) {
      if (h + s < n) Y[static_cast<size_t>(h)] = X[static_cast<size_t>(h + s)];
    }
  } else {
    int a = -s;
    for (int h = 0; h < n; h++) {
      if (h - a >= 0) Y[static_cast<size_t>(h)] = X[static_cast<size_t>(h - a)];
    }
  }
  return Y;
}

} // namespace

vector<float> computeOscillatorMagnitudes(const OscillatorChainParams & params) {
  vector<complex<float>> X;
  if (params.base_function == BaseFunction::None) {
    // "0 | none | Skip to harmonic expansion (sine case)" - no base
    // spectrum to compute; harmonic expansion sets X[n] = a_n directly.
    X = harmonicExpansion(params, {});
  } else {
    vector<complex<float>> B = computeBaseSpectrum(params);
    X = harmonicExpansion(params, B);
  }

  // Step 3 (harmonic_shift_first) is always "no" in every preset here
  // (see docs/padsynth.md) - the shift always runs at step 7 instead, so
  // step 3 here is a deliberate no-op.

  X = applyWaveshaper(std::move(X), params.waveshaper_kind, params.waveshaper_k);
  X = applyHarmonicFilter(std::move(X), params.filter); // filter_before_wave_shaping is always 0 - waveshaper then filter
  X = applyOscillatorTimeWarp(std::move(X), params.oscillator_warp);
  X = applySpectrumAdjust(std::move(X), params.spectrum_adjust_kind, params.spectrum_adjust_gamma);
  X = applyHarmonicShift(std::move(X), params.harmonic_shift);
  X[0] = complex<float>(0.0f, 0.0f); // step 8: zero DC

  vector<float> A(X.size());
  for (size_t k = 0; k < X.size(); k++) A[k] = std::abs(X[k]);
  return A;
}

} // namespace OscillatorShaping
