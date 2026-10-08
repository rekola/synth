#include "AdditiveModel.h"
#include "SpectralBandProfile.h"
#include "../dsp/HashField.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {
// One salt per HashField-drawn feature: the coordinate carries the per-string
// and per-partial variation, the salt keeps each axis uncorrelated with the
// others a note draws.
constexpr uint64_t kAdditivePhaseSalt = 0x5F1E8C2A6D93B047ull;
constexpr uint64_t kAdditiveUnisonDetuneSalt = 0x3B79A1D06E4C852Full;

constexpr float kMiddleC = 261.63f;
constexpr float kKeyboardOctaves = 87.0f / 12.0f; // 88 keys
constexpr float kPi = static_cast<float>(M_PI);
} // namespace

float additivePartialRatio(int n, int partials, float stretch, int edo_steps, bool tuning_matched) {
  float grid = tuningMatchedPartialRatio(n, edo_steps, partials, tuning_matched);
  return grid + static_cast<float>(n - 1) * stretch;
}

float unisonOffsetCents(int string, int strings, float spacing_cents, const NoteCoordinate & coord) {
  if (strings <= 1) return 0.0f;
  float base = (static_cast<float>(string) - 0.5f * static_cast<float>(strings - 1)) * spacing_cents;
  // Jitter of 15% of the spacing keeps adjacent strings within 70-130% of it.
  HashField field(kAdditiveUnisonDetuneSalt);
  float jitter = field.bipolar(coord.withInstance(string).toHashCoord(), paramId("additive_unison_detune"), spacing_cents * 0.15f);
  return base + jitter;
}

float keyboardPosition(float frequency) {
  if (frequency <= 0.0f) return 0.0f;
  return clamp(log2f(frequency / kMiddleC) / kKeyboardOctaves, -0.5f, 0.5f);
}

float stringAzimuthOffsetDeg(int string, int strings, float spacing_deg) {
  if (strings <= 1) return 0.0f;
  return (static_cast<float>(string) - 0.5f * static_cast<float>(strings - 1)) * spacing_deg;
}

vector<PartialSpec>
buildPartialSpecs(const AdditiveModelParams & params, const NoteContext & note) {
  const int strings = clamp(params.unison_voices, 1, 3);
  const int partials = max(0, params.partials);
  const float nyquist = note.sample_rate * 0.5f;
  const float tilt = params.tilt_db + params.velocity_tilt_db * (note.velocity - 0.5f);
  HashField phase_field(kAdditivePhaseSalt);

  vector<PartialSpec> specs;
  specs.reserve(static_cast<size_t>(strings * partials));
  for (int s = 0; s < strings; s++) {
    float string_f0 = note.frequency * powf(2.0f, unisonOffsetCents(s, strings, params.unison_detune_cents, note.coord) / 1200.0f);

    for (int n = 1; n <= partials; n++) {
      float freq = string_f0 * additivePartialRatio(n, partials, params.stretch, note.edo_steps, params.tuning_matched);
      if (freq >= nyquist) continue;

      // Shared among the strings so a wider unison isn't louder.
      float amplitude = powf(10.0f, tilt * log2f(static_cast<float>(n)) / 20.0f) / static_cast<float>(strings);
      float alpha = params.decay_a + params.decay_b * powf(freq, params.decay_p);
      // Distinct for every (string, partial) pair.
      float phase = phase_field.range(note.coord.withInstance(s * 4096 + n).toHashCoord(), paramId("additive_phase"), 0.0f, 2.0f * kPi);
      specs.push_back({freq, amplitude, alpha, phase, s});
    }
  }
  return specs;
}
