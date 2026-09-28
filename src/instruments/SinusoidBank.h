#ifndef _SINUSOIDBANK_H_
#define _SINUSOIDBANK_H_

#include "../model/NoteCoordinate.h"

#include <vector>
#include <cstddef>

// A bank of independently-decaying sinusoidal partials for one note event -
// the core DSP behind the <additive> instrument (Additive.h/AdditiveVoice.h).
// Genuinely per-voice (unlike PadSynth's shared/cached wavetable): each
// note's own decay clock and per-partial starting phases are independent,
// so a fresh SinusoidBank is built at note-on time from the resolved
// frequency/timbre/decay parameters and lives exactly as long as the voice
// that owns it.
//
// Sine generation: the coupled-form ("magic circle"/digital-resonator)
// recursion y[n] = coeff*y[n-1] - y[n-2] (coeff = 2*cos(w)), one instance
// per partial, rather than a per-sample sinf() call or a shared wavetable.
// Chosen over table-lookup for this specific use (PadSynth's own parallel
// stage uses a table instead, appropriate there since it caches one
// wavetable across many notes): a recursive oscillator has zero table/
// interpolation cost and its per-sample state (coeff/y1/y2) is trivial to
// lay out as flat parallel arrays, which is exactly the shape this bank
// needs anyway for its two-tier (partial-count x unison-voice) structure.
// The accepted tradeoff is slow numerical drift (phase/amplitude error
// accumulates every sample) - negligible here because every partial's own
// *amplitude* decays exponentially over the note's life (typically well
// under a second before a partial is culled - see below), so the window in
// which drift could become audible is always shorter than the partial's
// own remaining lifetime. No periodic renormalization is implemented.
//
// Data layout: every per-partial quantity lives in its own flat
// std::vector (struct-of-arrays, not array-of-structs) - coeff_/y1_/y2_ for
// the recursive oscillator state, amp_/decay_mult_/min_amp_ for the
// exponential-decay envelope - so the per-sample update loop over active
// partials in render() is a single, uniform, auto-vectorizable pass over
// contiguous memory, not a scattered struct walk.
//
// Two performance rules keep an old, mostly-decayed note cheap even at high
// polyphony: a partial whose post-inharmonicity/tuning-matched frequency
// already exceeds Nyquist is never added in the first place (built once at
// construction, in buildPartials()); a partial whose amplitude has decayed
// below -90dB relative to its own starting amplitude is culled from the
// active set (checked once per render() call, not per-sample) via a
// swap-with-last-active removal - decay is monotonic (amplitude only ever
// multiplies by a fixed sub-1 factor each sample), so once culled a partial
// never needs to reappear, and a plain unordered swap-remove is sufficient.
class SinusoidBank {
 public:
  struct Params {
    float frequency;           // fundamental, Hz
    int partial_count;         // 1-indexed partials 1..partial_count
    float spectral_tilt_db;    // dB/octave-ish rolloff on initial partial amplitude
    float inharmonicity_b;     // stretched-partial coefficient B, 0 = pure harmonic;
                               // only stretches partials above partial_limit
                               // when tuning_matched - see SinusoidBank.cpp's
                               // additivePartialRatio()
    int edo_steps;             // 0 = no tuning structure (Tuning::PERCUSSION)
    bool tuning_matched;
    int partial_limit;         // only the first N partials get tuning-snapped
    float decay_a, decay_b, decay_p; // alpha_n = decay_a + decay_b * f_n^decay_p (nepers/s)
    int unison_voices;         // 1-3
    float unison_detune_cents; // spread across unison_voices, meaningless when 1
    float sample_rate;
  };

  SinusoidBank(const Params & params, const NoteCoordinate & note_coord);

  // Adds this bank's output into out[0..frames) (mixes, does not zero
  // out[] itself - the caller, AdditiveVoice, owns that). Culls
  // fully-decayed partials once, at the end of the call.
  void render(float * out, int frames);

  bool isActive() const { return active_count_ > 0; }

  // Test-only accessor - number of partials still being computed (post
  // Nyquist-skip, post -90dB cull).
  int getActivePartialCountForTest() const { return active_count_; }
  // Test-only accessor - the current amplitude of the n-th (0-indexed, in
  // construction order) partial still active, or 0 if out of range - used
  // by SinusoidBankTests.cpp to measure per-partial decay envelopes.
  float getPartialAmplitudeForTest(int index) const;

 private:
  void buildPartials(const Params & params, const NoteCoordinate & note_coord);
  void addPartial(float freq_hz, float amplitude, float phase, float alpha_nepers_per_sec, float sample_rate);
  void cullDecayedPartials();

  std::vector<float> coeff_;      // 2*cos(2*pi*f/sr), per partial
  std::vector<float> y1_, y2_;    // recursive oscillator state (y[n-1], y[n-2])
  std::vector<float> amp_;        // current envelope amplitude
  std::vector<float> decay_mult_; // per-sample amplitude multiplier (exp(-alpha/sr))
  std::vector<float> min_amp_;    // -90dB-relative-to-start cull threshold
  int active_count_ = 0;
};

#endif
