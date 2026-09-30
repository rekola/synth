#ifndef _OSCILLATORSHAPINGCHAIN_H_
#define _OSCILLATORSHAPINGCHAIN_H_

#include <complex>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

// Clean-room reimplementation of ZynAddSubFX's OSCIL shaping chain, from
// its public algorithm description (per-preset numeric parameters, never
// ZynAddSubFX's own application source) - see docs/padsynth.md's own
// "What's real data vs. an approximation" section for the boundary this
// keeps to. Produces A[h], h = 1 .. kHarmonicCount - 1 (kHarmonicCount-1 =
// 255 total harmonics, from a fixed N=512-point oscillator table), the
// per-harmonic magnitude profile an imported PADsynth preset's own real
// oscillator shape actually has, before the anchored-spectral-envelope
// remap (main prompt/dsp/SpectralEnvelopeRemap.h) and PADsynth rendering
// (PadSynthTable.h) run on top of it.
//
// Every stage here is deterministic and allocation-light (each call builds
// its own small fixed-size (512-point) FFT plan via RealFFT - acceptable
// since this only ever runs at preset/table-build time, never per-sample
// or per-note); "the importer must reject any other value" is enforced by
// throwing std::invalid_argument for any enum field outside the values
// this file actually implements, rather than silently guessing.
namespace OscillatorShaping {

constexpr int kSize = 512;               // N
constexpr int kHarmonicCount = kSize / 2; // k = 0 .. kHarmonicCount-1 (255 = highest harmonic)

// base_function - only the values real imported presets actually use.
enum class BaseFunction {
  None = 0,           // "sine case" - skip straight to harmonic expansion, no B[k] needed
  ClippedTriangle = 1,
  PowerRamp = 4,
  GaussianPulse = 5,
  WarpedHalfSine = 7,
};

enum class WaveshaperKind { None = 0, Arctangent = 1, LogisticSigmoid = 14 };
enum class FilterKind { None = 0, ExponentialLowpass = 1, SingleHarmonicBoost = 13 };
enum class SpectrumAdjustKind { None = 0, PowerLaw = 1 };

// A time-domain warp (base warp or oscillator warp) shares the same
// (k1, phi, k3) parameterization - see docs/padsynth.md.
struct TimeWarp {
  bool enabled = false;
  float k1 = 0.0f;
  float phi = 0.0f;
  float k3 = 1.0f;
};

struct HarmonicFilterParams {
  FilterKind kind = FilterKind::None;
  // ExponentialLowpass: g_k = decay_base^k, replaced by g_k^10/threshold^9
  // when g_k < threshold.
  float decay_base = 1.0f;
  float threshold = 0.0f;
  // SingleHarmonicBoost: multiplies harmonic `boost_harmonic` by `boost_gain`.
  int boost_harmonic = 0;
  float boost_gain = 1.0f;
};

struct OscillatorChainParams {
  BaseFunction base_function = BaseFunction::None;
  // Interpretation depends on base_function - see docs/padsynth.md's own
  // per-shape formulas (already-derived a'/e/a, not the raw stored P).
  float base_shape_param = 0.0f;
  TimeWarp base_warp;

  // (n, a_n) pairs - harmonic number and its (already-derived, signed)
  // amplitude; base_function == None uses these directly as X[n], any
  // other base_function uses them to scale copies of the base spectrum
  // (harmonic expansion).
  std::vector<std::pair<int, float>> harmonics;

  WaveshaperKind waveshaper_kind = WaveshaperKind::None;
  float waveshaper_k = 0.0f;

  HarmonicFilterParams filter;

  TimeWarp oscillator_warp;

  SpectrumAdjustKind spectrum_adjust_kind = SpectrumAdjustKind::None;
  float spectrum_adjust_gamma = 1.0f;

  int harmonic_shift = 0; // signed, [-64, 64]
};

// Runs the full chain (steps 1-8 of the "Chain order" in docs/padsynth.md)
// and returns A[h] for h = 0 .. kHarmonicCount-1 (A[0] is always 0 - DC is
// zeroed at the end of the chain, step 8). This is *before* the envelope
// remap/postprocess (step 9, shared with the rest of this codebase's own
// PadSynth/Additive oscillators) and before the final RMS/peak
// normalization (step 10) - both applied by the caller.
std::vector<float> computeOscillatorMagnitudes(const OscillatorChainParams & params);

} // namespace OscillatorShaping

#endif
