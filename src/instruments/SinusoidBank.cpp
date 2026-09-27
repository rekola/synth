#include "SinusoidBank.h"
#include "SpectralBandProfile.h"
#include "../dsp/HashField.h"

#include <cmath>

using namespace std;

namespace {
// Fixed compile-time seed, not per-instance - see InstrumentVoice.h's own
// kNotePhaseSalt for the identical reasoning: the coordinate carries the
// per-partial/per-unison-voice variation, this salt just keeps this bank's
// own starting-phase axis decorrelated from every other HashField-derived
// value the same note might draw.
constexpr uint64_t kAdditivePhaseSalt = 0x5F1E8C2A6D93B047ull;
// A second, independent axis for the unison detune jitter (see
// NoteMultiplier.cpp's kDetuneSalt for the identical "one salt per
// HashField-drawn feature" convention) - deliberately not reused from the
// phase salt above, so a change to one axis's coordinate scheme can never
// accidentally correlate with the other.
constexpr uint64_t kAdditiveUnisonDetuneSalt = 0x3B79A1D06E4C852Full;

constexpr float kCullThresholdDb = -90.0f;
constexpr float kPi = 3.14159265358979323846f;

// -90dB relative amplitude, in linear gain.
inline float cullThresholdRatio() {
  static const float ratio = powf(10.0f, kCullThresholdDb / 20.0f);
  return ratio;
}

// Snaps partial n's frequency ratio to the song's own tuning, same formula
// as SpectralBandProfile.h's tuningMatchedPartialRatio() but generalized to
// take the *natural* (possibly inharmonicity-stretched) ratio rather than
// assuming it's exactly n - see this file's own doc comment on the header
// for why: inharmonicity computes the stretched ratio first, and
// tuning-matching (when enabled) snaps that, not the plain harmonic index.
// With inharmonicity_b == 0 this reduces to calling
// tuningMatchedPartialRatio() directly (natural ratio is exactly n), so the
// two paths agree exactly in the common case.
float additivePartialRatio(int n, float inharmonicity_b, int edo_steps, int partial_limit, bool tuning_matched) {
  if (inharmonicity_b <= 0.0f) {
    return tuningMatchedPartialRatio(n, edo_steps, partial_limit, tuning_matched);
  }

  float natural = static_cast<float>(n) * sqrtf(1.0f + inharmonicity_b * static_cast<float>(n) * static_cast<float>(n));
  if (!tuning_matched || edo_steps <= 0 || n > partial_limit) return natural;

  float steps = roundf(static_cast<float>(edo_steps) * log2f(natural));
  return powf(2.0f, steps / static_cast<float>(edo_steps));
}

// Symmetric, HashField-jittered detune spread across unison_voices copies,
// in cents - the same "several detuned copies spread by a dimensionless
// amount, jittered rather than perfectly even" shape NoteMultiplier.cpp
// uses for its own unison spread, simplified since this bank only ever
// needs a flat detune amount (no chord intervals). voice_index 0 with
// unison_voices == 1 always returns exactly 0 - a single "unison" voice is
// just the plain, undetuned case.
float unisonDetuneCentsFor(int voice_index, int unison_voices, float unison_detune_cents, const NoteCoordinate & note_coord) {
  if (unison_voices <= 1) return 0.0f;

  // Evenly spaced base offsets across [-detune, +detune], then a small
  // jitter (15% of the detune amount) so several unison copies don't beat
  // at a perfectly mechanical, evenly-spaced rate.
  float base = -unison_detune_cents + (2.0f * unison_detune_cents * static_cast<float>(voice_index)) / static_cast<float>(unison_voices - 1);
  HashField field(kAdditiveUnisonDetuneSalt);
  float jitter = field.bipolar(note_coord.withInstance(voice_index).toHashCoord(), paramId("additive_unison_detune"), unison_detune_cents * 0.15f);
  return base + jitter;
}
}

SinusoidBank::SinusoidBank(const Params & params, const NoteCoordinate & note_coord) {
  buildPartials(params, note_coord);
}

void
SinusoidBank::buildPartials(const Params & params, const NoteCoordinate & note_coord) {
  int unison_voices = params.unison_voices < 1 ? 1 : (params.unison_voices > 3 ? 3 : params.unison_voices);
  float nyquist = params.sample_rate * 0.5f;
  HashField phase_field(kAdditivePhaseSalt);

  for (int voice = 0; voice < unison_voices; voice++) {
    float detune_cents = unisonDetuneCentsFor(voice, unison_voices, params.unison_detune_cents, note_coord);
    float voice_f0 = params.frequency * powf(2.0f, detune_cents / 1200.0f);

    for (int n = 1; n <= params.partial_count; n++) {
      float ratio = additivePartialRatio(n, params.inharmonicity_b, params.edo_steps, params.partial_limit, params.tuning_matched);
      float freq_hz = voice_f0 * ratio;
      if (freq_hz >= nyquist) continue; // Nyquist skip - never rendered, never even allocated.

      // Spectral tilt: amplitude_n = 10^(tilt_db_per_oct * log2(n) / 20) -
      // n=1 (the fundamental) is always exactly 1.0 regardless of tilt.
      // Normalized by unison_voices so a wider unison doesn't get louder
      // just from having more simultaneous copies.
      float amplitude = powf(10.0f, params.spectral_tilt_db * log2f(static_cast<float>(n)) / 20.0f) / static_cast<float>(unison_voices);

      float alpha = params.decay_a + params.decay_b * powf(freq_hz, params.decay_p);

      // Combined (voice, partial) instance id - distinct for every
      // (unison voice, partial) pair this bank ever builds, well within
      // int range for the partial/unison counts this element supports.
      int instance_id = voice * 4096 + n;
      float phase = phase_field.range(note_coord.withInstance(instance_id).toHashCoord(), paramId("additive_phase"), 0.0f, 2.0f * kPi);

      addPartial(freq_hz, amplitude, phase, alpha, params.sample_rate);
    }
  }
}

void
SinusoidBank::addPartial(float freq_hz, float amplitude, float phase, float alpha_nepers_per_sec, float sample_rate) {
  float w = 2.0f * kPi * freq_hz / sample_rate;
  coeff_.push_back(2.0f * cosf(w));
  // Coupled-form init: y2_ holds y[-1] = sin(phase - w), y1_ holds
  // y[0] = sin(phase) - the first sample render() actually outputs.
  y2_.push_back(sinf(phase - w));
  y1_.push_back(sinf(phase));
  amp_.push_back(amplitude);
  decay_mult_.push_back(expf(-alpha_nepers_per_sec / sample_rate));
  min_amp_.push_back(amplitude * cullThresholdRatio());
  active_count_++;
}

void
SinusoidBank::render(float * out, int frames) {
  for (int k = 0; k < frames; k++) {
    float sum = 0.0f;
    for (int p = 0; p < active_count_; p++) {
      sum += y1_[static_cast<size_t>(p)] * amp_[static_cast<size_t>(p)];

      float next = coeff_[static_cast<size_t>(p)] * y1_[static_cast<size_t>(p)] - y2_[static_cast<size_t>(p)];
      y2_[static_cast<size_t>(p)] = y1_[static_cast<size_t>(p)];
      y1_[static_cast<size_t>(p)] = next;

      amp_[static_cast<size_t>(p)] *= decay_mult_[static_cast<size_t>(p)];
    }
    out[k] += sum;
  }

  cullDecayedPartials();
}

void
SinusoidBank::cullDecayedPartials() {
  int p = 0;
  while (p < active_count_) {
    if (amp_[static_cast<size_t>(p)] < min_amp_[static_cast<size_t>(p)]) {
      int last = active_count_ - 1;
      coeff_[static_cast<size_t>(p)] = coeff_[static_cast<size_t>(last)];
      y1_[static_cast<size_t>(p)] = y1_[static_cast<size_t>(last)];
      y2_[static_cast<size_t>(p)] = y2_[static_cast<size_t>(last)];
      amp_[static_cast<size_t>(p)] = amp_[static_cast<size_t>(last)];
      decay_mult_[static_cast<size_t>(p)] = decay_mult_[static_cast<size_t>(last)];
      min_amp_[static_cast<size_t>(p)] = min_amp_[static_cast<size_t>(last)];
      active_count_--;
      // Don't advance p - the swapped-in value (formerly `last`) needs its
      // own check too.
    } else {
      p++;
    }
  }
}

float
SinusoidBank::getPartialAmplitudeForTest(int index) const {
  if (index < 0 || index >= active_count_) return 0.0f;
  return amp_[static_cast<size_t>(index)];
}
