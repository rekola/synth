#include "AdditiveModel.h"
#include "SpectralBandProfile.h"
#include "../dsp/HashField.h"

#include <algorithm>
#include <cmath>
#include <sstream>

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
constexpr double kPiD = M_PI;
// Hammer speed is taken as proportional to velocity; 0.5 is the reference.
constexpr float kReferenceVelocity = 0.5f;
// Root-sum-square amplitude of a note's modes: leaves headroom for a
// four-note chord of notes whose energy sits in one or two partials.
constexpr double kNoteRms = 0.5;
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

float bodyAzimuthOffsetDeg(int mode, int modes, float width_deg) {
  if (modes <= 1) return 0.0f;
  int slot = mode % 2 == 0 ? mode / 2 : modes - 1 - mode / 2;
  return (static_cast<float>(slot) / static_cast<float>(modes - 1) - 0.5f) * width_deg;
}

float lowpassGain(float frequency_hz, float cutoff_hz) {
  if (cutoff_hz <= 0.0f) return 1.0f;
  float r = frequency_hz / cutoff_hz;
  return 1.0f / sqrtf(1.0f + r * r * r * r);
}

float hammerCutoffHz(const AdditiveModelParams & params, float frequency, float velocity) {
  float v = max(velocity, 0.05f) / kReferenceVelocity;
  float key = frequency > 0.0f ? frequency / kMiddleC : 1.0f;
  return params.hammer_cutoff_hz * powf(key, params.hammer_tracking) * powf(v, params.hammer_velocity);
}

float hammerVelocityExponent(float felt_exponent) {
  return (felt_exponent - 1.0f) / (felt_exponent + 1.0f);
}

vector<float>
parseModeRatios(const string & text) {
  vector<float> out;
  istringstream in(text);
  float v;
  while (in >> v) {
    if (v > 0.0f) out.push_back(v);
  }
  return out;
}

vector<PartialSpec>
buildPartialSpecs(const AdditiveModelParams & params, const NoteContext & note) {
  const int strings = clamp(params.unison_voices, 1, 3);
  const bool bar = !params.modes.empty();
  const int partials = max(0, bar ? min(params.partials, static_cast<int>(params.modes.size())) : params.partials);
  const float nyquist = note.sample_rate * 0.5f;
  const float cutoff = hammerCutoffHz(params, note.frequency, note.velocity);
  HashField phase_field(kAdditivePhaseSalt);

  // Frequency ratio of mode n of the string or bar.
  auto ratio = [&](int n) {
    return bar ? params.modes[static_cast<size_t>(n - 1)]
               : additivePartialRatio(n, partials, params.stretch, note.edo_steps, params.tuning_matched);
  };

  // Starting level of each mode before normalising: the excitation's
  // spectrum at the mode's number (the point it is struck or plucked at
  // nulls the modes with a node there) and frequency. A bar has no such
  // nodes in this model.
  vector<double> level(static_cast<size_t>(partials) + 1, 0.0);
  double peak = 0.0, power = 0.0;
  for (int n = 1; n <= partials; n++) {
    float freq = note.frequency * ratio(n);
    double comb = bar ? 1.0 : fabs(sin(kPiD * n * params.strike));
    double spectrum;
    if (params.excitation == Excitation::Pluck) {
      // A plucked string starts as a displaced triangle: modes fall as 1/n^2.
      spectrum = (bar ? 1.0 : 1.0 / (static_cast<double>(n) * n)) * lowpassGain(freq, params.pluck_cutoff_hz);
    } else {
      spectrum = lowpassGain(freq, cutoff);
    }
    level[static_cast<size_t>(n)] = comb * spectrum;
    peak = max(peak, level[static_cast<size_t>(n)]);
  }
  if (peak <= 0.0) return {};

  // Modes starting far below the strongest are not built; the rest are
  // scaled so every note has the same total power, whatever its key,
  // velocity or how many modes fit under the corner.
  const double floor = peak * pow(10.0, params.partial_floor_db / 20.0);
  for (int n = 1; n <= partials; n++) {
    if (level[static_cast<size_t>(n)] < floor) level[static_cast<size_t>(n)] = 0.0;
    power += level[static_cast<size_t>(n)] * level[static_cast<size_t>(n)];
  }
  const double scale = kNoteRms / sqrt(power);

  vector<PartialSpec> specs;
  specs.reserve(static_cast<size_t>(strings * partials) + (params.thump > 0.0f ? params.body.size() : 0));
  float strongest = 0.0f;
  const float track = note.frequency > 0.0f ? powf(note.frequency / kMiddleC, params.decay_tracking) : 1.0f;
  for (int s = 0; s < strings; s++) {
    float string_f0 = note.frequency * powf(2.0f, unisonOffsetCents(s, strings, params.unison_detune_cents, note.coord) / 1200.0f);
    // Outer strings decay faster and slower than the middle one: summed,
    // the unequal rates give a fast first decay and a slow aftersound.
    float place = strings > 1 ? 2.0f * static_cast<float>(s) / static_cast<float>(strings - 1) - 1.0f : 0.0f;
    float rate = track * (1.0f + params.decay_spread * place);

    for (int n = 1; n <= partials; n++) {
      if (level[static_cast<size_t>(n)] <= 0.0) continue;
      float freq = string_f0 * ratio(n);
      if (freq >= nyquist) continue;

      // Shared among the strings so a wider unison isn't louder.
      float amplitude = static_cast<float>(level[static_cast<size_t>(n)] * scale) / static_cast<float>(strings);
      float alpha = (params.decay_a + params.decay_b * powf(freq, params.decay_p)) * rate;
      // Distinct for every (string, partial) pair.
      float phase = phase_field.range(note.coord.withInstance(s * 4096 + n).toHashCoord(), paramId("additive_phase"), 0.0f, 2.0f * kPi);
      specs.push_back({freq, amplitude, alpha, phase, s});
      strongest = max(strongest, amplitude);
    }
  }

  if (params.thump > 0.0f) {
    for (size_t j = 0; j < params.body.size(); j++) {
      const BodyMode & mode = params.body[j];
      if (mode.frequency_hz >= nyquist) continue;
      float phase = phase_field.range(note.coord.withInstance(static_cast<int>(65536 + j)).toHashCoord(), paramId("additive_body_phase"), 0.0f, 2.0f * kPi);
      specs.push_back({mode.frequency_hz, params.thump * mode.amplitude * strongest, mode.alpha, phase, strings + static_cast<int>(j)});
    }
  }
  return specs;
}
