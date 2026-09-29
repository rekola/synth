#include "TestFramework.h"

#include "../src/instruments/PadSynthWavetable.h"
#include "../src/instruments/SpectralBandProfile.h"
#include "../src/dsp/RealFFT.h"

#include <cmath>
#include <complex>
#include <vector>

using namespace std;

namespace {

// Magnitude of bin `i` of `table`'s own spectrum, via a fresh forward FFT
// of the same size - used only to locate spectral peaks for the
// tuning-matched-partial test below, never by PadSynthWavetable itself
// (which only ever needs inverse()).
vector<float> magnitudeSpectrum(const vector<float> & table) {
  RealFFT<float> fft(table.size());
  auto & spectrum = fft.forward(table);
  vector<float> magnitude(spectrum.size());
  for (size_t i = 0; i < spectrum.size(); i++) magnitude[i] = std::abs(spectrum[i]);
  return magnitude;
}

// Sub-bin-accurate peak location (in bins) nearest `expected_bin`, via a
// coarse argmax over +/-search_radius bins followed by quadratic
// (parabolic) interpolation across the three bins straddling that argmax -
// needed because a plain integer-bin argmax alone can be off by up to half
// a bin, which at this table's own bin spacing is a large enough fraction
// of a cent to make a +/-1 cent assertion flaky.
float findPeakBin(const vector<float> & magnitude, float expected_bin, int search_radius) {
  int center = static_cast<int>(std::lround(expected_bin));
  int lo = std::max(1, center - search_radius);
  int hi = std::min(static_cast<int>(magnitude.size()) - 2, center + search_radius);

  int best = lo;
  for (int i = lo; i <= hi; i++) {
    if (magnitude[static_cast<size_t>(i)] > magnitude[static_cast<size_t>(best)]) best = i;
  }

  float m_minus = magnitude[static_cast<size_t>(best - 1)];
  float m_zero = magnitude[static_cast<size_t>(best)];
  float m_plus = magnitude[static_cast<size_t>(best + 1)];
  float denom = (m_minus - 2.0f * m_zero + m_plus);
  float delta = (std::fabs(denom) > 1e-12f) ? 0.5f * (m_minus - m_plus) / denom : 0.0f;
  return static_cast<float>(best) + delta;
}

} // namespace

TEST(padsynth_wavetable_generation_is_deterministic) {
  PadSynthWavetable a(48000, 16, 40.0f, 0.8f, 31, 8, true, 12345);
  PadSynthWavetable b(48000, 16, 40.0f, 0.8f, 31, 8, true, 12345);

  const auto & table_a = a.getTable(220.0f);
  const auto & table_b = b.getTable(220.0f);

  CHECK(table_a.size() == table_b.size());
  bool all_equal = true;
  for (size_t i = 0; i < table_a.size(); i++) {
    if (table_a[i] != table_b[i]) { all_equal = false; break; }
  }
  CHECK(all_equal);
}

TEST(padsynth_wavetable_generation_is_deterministic_across_repeated_calls_on_the_same_instance) {
  // getTable() caches - a second call for the same pitch region must
  // return the exact same content, not silently regenerate with fresh
  // (and by construction still-deterministic, but this confirms the cache
  // path itself doesn't disturb anything) random phases.
  PadSynthWavetable table(44100, 24, 30.0f, 1.0f, 19, 6, true, 987654321ull);
  auto & first = table.getTable(330.0f);
  vector<float> copy_of_first(first.begin(), first.end());
  auto & second = table.getTable(330.0f);
  CHECK(copy_of_first.size() == second.size());
  bool all_equal = true;
  for (size_t i = 0; i < copy_of_first.size(); i++) {
    if (copy_of_first[i] != second[i]) { all_equal = false; break; }
  }
  CHECK(all_equal);
}

TEST(padsynth_wavetable_different_seeds_produce_different_tables) {
  PadSynthWavetable a(48000, 16, 40.0f, 0.8f, 31, 8, true, 1);
  PadSynthWavetable b(48000, 16, 40.0f, 0.8f, 31, 8, true, 2);

  const auto & table_a = a.getTable(220.0f);
  const auto & table_b = b.getTable(220.0f);

  bool any_different = false;
  for (size_t i = 0; i < table_a.size(); i++) {
    if (table_a[i] != table_b[i]) { any_different = true; break; }
  }
  CHECK(any_different);
}

TEST(padsynth_wavetable_scales_size_with_sample_rate) {
  // >= 2^18 at 48kHz (the task's own floor); a higher output rate must get
  // an equivalently long/seamless table, not the same literal sample count.
  PadSynthWavetable at48k(48000, 8, 40.0f, 0.8f, 31, 8, true, 1);
  PadSynthWavetable at96k(96000, 8, 40.0f, 0.8f, 31, 8, true, 1);

  CHECK(at48k.getTableSize() >= (size_t(1) << 18));
  CHECK(at96k.getTableSize() >= at48k.getTableSize() * 2);
}

TEST(padsynth_tuning_matched_partials_land_within_one_cent_of_31edo_steps) {
  const int edo_steps = 31;
  const int partial_limit = 8;
  // A fairly high octave region keeps this table's fixed bin spacing
  // (sample_rate/table_size) a small fraction of a cent relative to the
  // fundamental, so peak-picking precision doesn't dominate the ±1 cent
  // budget the assertion below actually cares about (see findPeakBin()'s
  // own comment on why quadratic interpolation is used regardless).
  const float f0_request = 2093.0f;
  // Narrow bandwidth keeps each harmonic's own peak sharp and easy to
  // locate precisely - a wide, deliberately-detuned-sounding preset would
  // still have its energy centered at the same frequency, just spread
  // out, but a narrow band makes this test's own measurement more robust.
  PadSynthWavetable table(48000, 16, 6.0f, 0.5f, edo_steps, partial_limit, true, 42);

  auto & wave = table.getTable(f0_request);
  float f0 = table.tableBaseFrequency(f0_request);
  auto magnitude = magnitudeSpectrum(wave);
  float bin_hz = 48000.0f / static_cast<float>(wave.size());
  float step_cents = 1200.0f / static_cast<float>(edo_steps);

  for (int n = 1; n <= partial_limit; n++) {
    float ratio = tuningMatchedPartialRatio(n, edo_steps, partial_limit, true);
    float expected_freq = f0 * ratio;
    float expected_bin = expected_freq / bin_hz;

    float peak_bin = findPeakBin(magnitude, expected_bin, 40);
    float peak_freq = peak_bin * bin_hz;
    float cents_from_fundamental = 1200.0f * std::log2(peak_freq / f0);

    float nearest_step = std::round(cents_from_fundamental / step_cents) * step_cents;
    CHECK_NEAR(cents_from_fundamental, nearest_step, 1.0f);
  }
}

TEST(padsynth_untuned_partials_are_plain_harmonics) {
  // With tuning_matched off, tuningMatchedPartialRatio() (and therefore
  // this table's own generation) must fall back to plain integer harmonic
  // ratios - checked directly against the shared helper, not by
  // re-measuring the spectrum (SpectralBandProfile.h's own unit already
  // covers that function in isolation; this just confirms nothing in
  // PadSynthWavetable second-guesses it).
  for (int n = 1; n <= 8; n++) {
    CHECK(tuningMatchedPartialRatio(n, 31, 8, false) == static_cast<float>(n));
  }
}

TEST(padsynth_partial_limit_leaves_higher_harmonics_unmatched) {
  // n <= partial_limit gets bent toward a scale step; n > partial_limit
  // stays a plain harmonic even with tuning_matched on.
  CHECK(tuningMatchedPartialRatio(9, 31, 8, true) == 9.0f);
  CHECK(tuningMatchedPartialRatio(1, 31, 8, true) == 1.0f); // harmonic 1 is always exactly the fundamental
}

TEST(padsynth_harmonic_jitter_is_deterministic) {
  // Same reasoning as padsynth_wavetable_generation_is_deterministic above
  // - a nonzero harmonic_amplitude_jitter draws from HashField, not
  // std::rand()/<random>, so two identically-configured instances must
  // still produce byte-identical tables.
  PadSynthWavetable a(48000, 16, 40.0f, 0.8f, 31, 8, true, 12345, 1.0f, {}, 0.3f);
  PadSynthWavetable b(48000, 16, 40.0f, 0.8f, 31, 8, true, 12345, 1.0f, {}, 0.3f);

  const auto & table_a = a.getTable(220.0f);
  const auto & table_b = b.getTable(220.0f);
  CHECK(table_a.size() == table_b.size());
  for (size_t i = 0; i < table_a.size(); i++) CHECK(table_a[i] == table_b[i]);
}

TEST(padsynth_harmonic_jitter_changes_per_harmonic_amplitude) {
  // With jitter off, every harmonic's peak amplitude should fall off
  // smoothly as 1/n^amplitude_rolloff_exponent (no formants here) - with
  // it on, at least one harmonic's peak must measurably deviate from that
  // smooth prediction (see PadSynthWavetable.h's own doc comment: this is
  // the entire point of harmonic_amplitude_jitter - irregular,
  // non-smooth per-harmonic detail no rolloff-exponent curve alone can
  // produce).
  const float f0_request = 2093.0f;
  const float rolloff = 1.0f;
  const int partial_count = 8;

  PadSynthWavetable plain(48000, partial_count, 6.0f, 0.5f, 31, 8, true, 42, rolloff);
  PadSynthWavetable jittered(48000, partial_count, 6.0f, 0.5f, 31, 8, true, 42, rolloff, {}, 0.3f);

  auto & wave_plain = plain.getTable(f0_request);
  auto & wave_jittered = jittered.getTable(f0_request);
  float f0 = plain.tableBaseFrequency(f0_request);
  auto magnitude_plain = magnitudeSpectrum(wave_plain);
  auto magnitude_jittered = magnitudeSpectrum(wave_jittered);
  float bin_hz = 48000.0f / static_cast<float>(wave_plain.size());

  bool any_deviates = false;
  for (int n = 1; n <= partial_count; n++) {
    float expected_bin = (f0 * static_cast<float>(n)) / bin_hz;
    float peak_plain = magnitude_plain[static_cast<size_t>(std::lround(findPeakBin(magnitude_plain, expected_bin, 40)))];
    float peak_jittered = magnitude_jittered[static_cast<size_t>(std::lround(findPeakBin(magnitude_jittered, expected_bin, 40)))];
    if (peak_plain > 0.0f) {
      float ratio = peak_jittered / peak_plain;
      if (ratio < 0.85f || ratio > 1.15f) any_deviates = true;
    }
  }
  CHECK(any_deviates);
}

TEST(padsynth_explicit_harmonic_amplitudes_produce_a_sparse_spectrum) {
  // A real ported patch (Church Organ 3, PadSynthPresets.h's own
  // kChurchOrgan) has energy at harmonics 1/2/4 but silence at harmonic 3
  // - something no 1/n^rolloff formula could ever produce, since every
  // harmonic gets *some* nonzero amplitude under that formula. Checked
  // directly: harmonics 1/2/4 (listed, nonzero) must have real spectral
  // peaks; harmonic 3 (not listed - implicitly 0) must not.
  const float f0_request = 2093.0f;
  const int partial_count = 4;
  std::vector<float> harmonic_amplitudes = { 1.0f, 0.6f, 0.0f, 0.4f }; // 1, 2, (3 silent), 4

  PadSynthWavetable table(48000, partial_count, 6.0f, 0.5f, 31, 8, true, 42,
                           1.0f, {}, 0.0f, harmonic_amplitudes);
  auto & wave = table.getTable(f0_request);
  float f0 = table.tableBaseFrequency(f0_request);
  auto magnitude = magnitudeSpectrum(wave);
  float bin_hz = 48000.0f / static_cast<float>(wave.size());

  auto peakAt = [&](int n) {
    float expected_bin = (f0 * static_cast<float>(n)) / bin_hz;
    return magnitude[static_cast<size_t>(std::lround(findPeakBin(magnitude, expected_bin, 40)))];
  };

  float peak1 = peakAt(1), peak2 = peakAt(2), peak3 = peakAt(3), peak4 = peakAt(4);
  CHECK(peak1 > 0.0f);
  CHECK(peak2 > 0.0f);
  CHECK(peak4 > 0.0f);
  // Harmonic 3 has no real peak of its own - whatever tiny energy leaks in
  // from neighboring bands' own Gaussian tails must stay far below a real
  // harmonic's own peak.
  CHECK(peak3 < peak1 * 0.05f);
}

static float rms(const std::vector<float> & table) {
  double sum_sq = 0.0;
  for (float s : table) sum_sq += static_cast<double>(s) * static_cast<double>(s);
  return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(table.size())));
}

TEST(padsynth_envelope_remap_does_not_change_level) {
  // Requirement 8 (Level): enabling the remap must not itself jump
  // loudness - both tables are peak-normalized to the same fixed target
  // (PadSynthWavetable.cpp's own kTargetPeak), so their RMS should stay
  // within the same rough envelope even though the underlying harmonic
  // shape differs.
  std::vector<float> harmonics = { 1.0f, 0.75f, 0.5f, 0.4f, 0.3f, 0.2f };

  PadSynthWavetable off(48000, 6, 21.2f, 0.5f, 31, 8, true, 7,
                         1.0f, {}, 0.0f, harmonics,
                         /* envelope_anchor_hz */ 0.0f, /* envelope_tracking */ 0.0f);
  PadSynthWavetable on(48000, 6, 21.2f, 0.5f, 31, 8, true, 7,
                        1.0f, {}, 0.0f, harmonics,
                        /* envelope_anchor_hz */ 233.2f, /* envelope_tracking */ 0.634f);

  float rms_off = rms(off.getTable(440.0f));
  float rms_on = rms(on.getTable(440.0f));
  CHECK(rms_off > 0.0f);
  CHECK(rms_on > 0.0f);
  float ratio = rms_on / rms_off;
  CHECK(ratio > 0.3f && ratio < 3.0f);
}
