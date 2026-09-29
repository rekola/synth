#include "SinusoidBank.h"
#include "SpectralBandProfile.h"
#include "../dsp/HashField.h"
#include "../dsp/SpectralEnvelopeRemap.h"

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

// Partial n's frequency ratio, folding in inharmonicity B (default 0 - pure
// harmonic/tuning-matched, no stretch). When tuning-matching is off (or
// there's no scale to map onto, edo_steps<=0), B stretches every partial via
// the standard stiff-string formula n*sqrt(1+B*n^2) - simple, since nothing
// is being snapped to begin with.
//
// When tuning-matching is on, B has no effect at all within the
// tuning-matched region (n <= partial_limit): every partial there stays
// exactly on its scale step, same as B==0 - a real stiff string's own
// fundamental drifts sharp of its "ideal" pitch under the plain formula
// above (see the session discussion this replaced), which is physically
// accurate but meant a note's own audible pitch could drift with B; pinning
// the matched region to the scale entirely avoids that. Above the limit,
// the stretch is applied as a direct cents-space shift referenced from the
// limit partial's own snapped cents value - 866*B*(n^2 - n_lim^2) - so the
// curve is exactly continuous at n_lim (the shift is 0 there, matching
// tuningMatchedPartialRatio(n_lim, ...) exactly) rather than jumping from a
// snapped value to an unrelated stretched one. 866 = 1200/(2*ln 2), the
// small-angle linearization of 1200*log2(sqrt(1+B*n^2)) directly in cents -
// avoids a sqrt/log per partial and matches the plain stiff-string formula
// closely for the small B values real strings actually have.
float additivePartialRatio(int n, float inharmonicity_b, int edo_steps, int partial_limit, bool tuning_matched) {
  if (inharmonicity_b <= 0.0f) {
    return tuningMatchedPartialRatio(n, edo_steps, partial_limit, tuning_matched);
  }

  if (!tuning_matched || edo_steps <= 0 || partial_limit < 1) {
    return static_cast<float>(n) * sqrtf(1.0f + inharmonicity_b * static_cast<float>(n) * static_cast<float>(n));
  }

  if (n <= partial_limit) {
    return tuningMatchedPartialRatio(n, edo_steps, partial_limit, tuning_matched);
  }

  float limit_steps = roundf(static_cast<float>(edo_steps) * log2f(static_cast<float>(partial_limit)));
  float limit_cents = limit_steps / static_cast<float>(edo_steps) * 1200.0f;
  float n_f = static_cast<float>(n), limit_f = static_cast<float>(partial_limit);
  float cents = limit_cents + 866.0f * inharmonicity_b * (n_f * n_f - limit_f * limit_f);
  return powf(2.0f, cents / 1200.0f);
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

  // Shaping (spectral tilt) then the anchored spectral-envelope remap/
  // postprocess, both evaluated once here against this note's own real
  // frequency (not per unison voice - a detuned unison copy differs only
  // by a few cents, negligible for where an absolute-Hz-anchored envelope
  // lands) - see dsp/SpectralEnvelopeRemap.h's own doc comment for why
  // this happens before per-partial frequency/decay computation below.
  // Additive's own partial phases are independent random draws (see
  // addPartial()/phase_field below, one draw per (voice, partial)), so the
  // remap's scatter branch accumulates in power here too, same reasoning
  // as PadSynthWavetable.cpp's own identical choice.
  std::vector<float> prototype(static_cast<size_t>(std::max(0, params.partial_count)), 0.0f);
  for (int n = 1; n <= params.partial_count; n++) {
    // n=1 (the fundamental) is always exactly 1.0 regardless of tilt.
    prototype[static_cast<size_t>(n - 1)] = powf(10.0f, params.spectral_tilt_db * log2f(static_cast<float>(n)) / 20.0f);
  }
  std::vector<float> remapped;
  if (params.envelope_anchor_hz > 0.0f && params.envelope_tracking != 0.0f) {
    anchoredSpectralEnvelopeRemap(prototype, params.frequency, params.envelope_anchor_hz, params.envelope_tracking,
                                   /* accumulate_in_power */ true, remapped);
  } else {
    remapped = prototype;
  }
  std::vector<float> shaped;
  switch (params.postprocess_kind) {
    case SpectralPostprocessKind::ResidueClassWeighting:
      shaped = remapped;
      residueClassWeighting(shaped, params.postprocess_n, params.postprocess_r, params.postprocess_amount);
      break;
    case SpectralPostprocessKind::StretchMix:
      stretchMix(remapped, params.postprocess_n, params.postprocess_amount, shaped);
      break;
    case SpectralPostprocessKind::None:
    default:
      shaped = remapped;
      break;
  }

  for (int voice = 0; voice < unison_voices; voice++) {
    float detune_cents = unisonDetuneCentsFor(voice, unison_voices, params.unison_detune_cents, note_coord);
    float voice_f0 = params.frequency * powf(2.0f, detune_cents / 1200.0f);

    for (int n = 1; n <= params.partial_count; n++) {
      float ratio = additivePartialRatio(n, params.inharmonicity_b, params.edo_steps, params.partial_limit, params.tuning_matched);
      float freq_hz = voice_f0 * ratio;
      if (freq_hz >= nyquist) continue; // Nyquist skip - never rendered, never even allocated.

      // Normalized by unison_voices so a wider unison doesn't get louder
      // just from having more simultaneous copies.
      float amplitude = shaped[static_cast<size_t>(n - 1)] / static_cast<float>(unison_voices);

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
