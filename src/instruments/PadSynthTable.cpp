#include "PadSynthTable.h"

#include "SpectralBandProfile.h"
#include "../dsp/HashField.h"
#include "../dsp/RealFFT.h"

#include <algorithm>
#include <cmath>

using namespace std;
using namespace OscillatorShaping;
using namespace PadSynthProfile;

namespace {
constexpr uint64_t kPadSynthProfilePhaseSalt = 0x8C4F2B7A19E6D3F0ull;
// The final rendered waveform's target RMS level, per docs/padsynth.md's
// own literal "RMS-normalize" step 4. Chosen empirically low enough that
// a rich, many-partial preset (Church Organ's own dense harmonic stack)
// doesn't clip once resampled and gained by a voice - see PadSynthVoice.h's
// own `level` contract ("level=1.0 means roughly full scale").
constexpr float kTargetRMS = 0.2f;
} // namespace

PadSynthTable::PadSynthTable(int sample_rate, PadSynthParams params)
  : sample_rate_(sample_rate), params_(std::move(params)) {
}

const std::vector<float> &
PadSynthTable::oscillatorMagnitudes() const {
  if (!oscillator_magnitudes_built_) {
    // Steps 1-8 of the chain never depend on frequency - see this class's
    // own header comment - so this is computed once and shared by every
    // sample point's own step 9/PADsynth placement.
    oscillator_magnitudes_ = computeOscillatorMagnitudes(params_.oscillator);
    oscillator_magnitudes_built_ = true;
  }
  return oscillator_magnitudes_;
}

float
PadSynthTable::sampleFrequency(int j) const {
  int M = sampleCount();
  float O = static_cast<float>(params_.octaves);
  return params_.base_frequency_hz * std::exp2(
    static_cast<float>(j) * O / static_cast<float>(M) - static_cast<float>(M - 1) * O / (2.0f * static_cast<float>(M)));
}

int
PadSynthTable::nearestSampleIndex(float f0) const {
  int M = sampleCount();
  float O = static_cast<float>(params_.octaves);
  float safe_f0 = f0 > 0.0f ? f0 : params_.base_frequency_hz;
  // Invert sampleFrequency(j): j = (log2(f0/f_b) + (M-1)*O/(2M)) * M/O.
  float target = (std::log2(safe_f0 / params_.base_frequency_hz) + static_cast<float>(M - 1) * O / (2.0f * static_cast<float>(M)))
    * static_cast<float>(M) / O;
  int j = static_cast<int>(std::lround(target));
  return std::max(0, std::min(M - 1, j));
}

float
PadSynthTable::tableBaseFrequency(float f0) const {
  return sampleFrequency(nearestSampleIndex(f0));
}

const std::vector<float> &
PadSynthTable::getTable(float f0) const {
  int j = nearestSampleIndex(f0);
  auto it = tables_.find(j);
  if (it != tables_.end()) return it->second;
  auto [inserted, ok] = tables_.emplace(j, renderSample(j));
  (void)ok;
  return inserted->second;
}

std::vector<float>
PadSynthTable::renderSample(int j) const {
  float f_j = sampleFrequency(j);
  const std::vector<float> & prototype = oscillatorMagnitudes();

  // Step 9: anchored spectral-envelope remap + postprocess, evaluated at
  // this sample's own base frequency f_j.
  std::vector<float> remapped;
  if (params_.envelope_anchor_hz > 0.0f && params_.envelope_tracking != 0.0f) {
    anchoredSpectralEnvelopeRemap(prototype, f_j, params_.envelope_anchor_hz, params_.envelope_tracking,
                                   /* accumulate_in_power */ true, remapped);
  } else {
    remapped = prototype;
  }
  std::vector<float> shaped;
  switch (params_.postprocess_kind) {
    case SpectralPostprocessKind::ResidueClassWeighting:
      shaped = remapped;
      residueClassWeighting(shaped, params_.postprocess_n, params_.postprocess_r, params_.postprocess_amount);
      break;
    case SpectralPostprocessKind::StretchMix:
      stretchMix(remapped, params_.postprocess_n, params_.postprocess_amount, shaped);
      break;
    case SpectralPostprocessKind::None:
    default:
      shaped = remapped;
      break;
  }

  // Step 10: normalize so the peak of A[h], h >= 1, is 1.
  float peak = 0.0f;
  for (size_t h = 1; h < shaped.size(); h++) peak = std::max(peak, shaped[h]);
  if (peak > 0.0f) {
    for (float & v : shaped) v /= peak;
  }

  // PADsynth rendering: the partial profile p[] and its own width
  // correction alpha are frequency-independent too (same reasoning as
  // oscillatorMagnitudes() above) - built once per render here rather
  // than cached separately, since renderSample() itself is only ever
  // called once per sample index (getTable() caches the result).
  auto profile = buildProfile(params_.profile);
  float alpha = computeProfileAlpha(profile, params_.profile.autoscale);

  int L = params_.table_length;
  int S = L / 2;
  float SR = static_cast<float>(sample_rate_);

  std::vector<float> amplitude_spectrum(static_cast<size_t>(S), 0.0f);

  for (int h = 1; ; h++) {
    if (h >= static_cast<int>(shaped.size())) break; // no oscillator data beyond this - nothing further to place
    float g_h = partialPosition(h, params_.position);
    g_h = tuningMatchedPartialPosition(g_h, params_.edo_steps, params_.tuning_matched);
    float phi_h = g_h * f_j;
    if (phi_h > 0.49999f * SR || phi_h < 20.0f) break; // stop entirely
    if (shaped[static_cast<size_t>(h)] < 1e-4f) continue; // skip

    placePartial(amplitude_spectrum, shaped[static_cast<size_t>(h)], phi_h, params_.bandwidth_cents, alpha, profile, SR);
  }

  amplitude_spectrum[0] = 0.0f;

  RealFFT<float> fft(static_cast<size_t>(L));
  HashField phase_field(kPadSynthProfilePhaseSalt ^ params_.seed ^ (static_cast<uint64_t>(static_cast<uint32_t>(j)) << 32));
  std::vector<std::complex<float>> spectrum(fft.binCount(), std::complex<float>(0.0f, 0.0f));
  for (int bin = 0; bin < S; bin++) {
    float amp = amplitude_spectrum[static_cast<size_t>(bin)];
    if (amp <= 0.0f) continue;
    float phase = phase_field.range(static_cast<int64_t>(bin), paramId("padsynth_phase"), 0.0f, 2.0f * static_cast<float>(M_PI));
    spectrum[static_cast<size_t>(bin)] = std::complex<float>(amp * std::cos(phase), amp * std::sin(phase));
  }

  auto table = fft.inverse(spectrum);
  double sum_sq = 0.0;
  for (float s : table) sum_sq += static_cast<double>(s) * static_cast<double>(s);
  float rms = static_cast<float>(std::sqrt(sum_sq / static_cast<double>(table.size())));
  std::vector<float> result(table.begin(), table.end());
  if (rms > 0.0f) {
    float scale = kTargetRMS / rms;
    for (float & s : result) s *= scale;
  }
  return result;
}
