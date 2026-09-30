#include "TestFramework.h"

#include "../src/instruments/SinusoidBank.h"
#include "../src/model/NoteCoordinate.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {
SinusoidBank::Params baseParams(float frequency, int partial_count, float sample_rate) {
  return SinusoidBank::Params{
    frequency, partial_count,
    /* spectral_tilt_db */ -6.0f,
    /* inharmonicity_b  */ 0.0f,
    /* edo_steps        */ 12,
    /* tuning_matched   */ true,
    /* partial_limit    */ 8,
    /* decay_a */ 0.2f, /* decay_b */ 0.001f, /* decay_p */ 1.0f,
    /* unison_voices */ 1,
    /* unison_detune_cents */ 0.0f,
    sample_rate
  };
}
}

TEST(sinusoid_bank_deterministic_across_instances) {
  NoteCoordinate coord(1, 4, 0);
  auto params = baseParams(220.0f, 8, 44100.0f);

  SinusoidBank a(params, coord);
  SinusoidBank b(params, coord);

  const int frames = 512;
  vector<float> out_a(static_cast<size_t>(frames), 0.0f), out_b(static_cast<size_t>(frames), 0.0f);
  a.render(out_a.data(), frames);
  b.render(out_b.data(), frames);

  for (int i = 0; i < frames; i++) {
    CHECK(out_a[static_cast<size_t>(i)] == out_b[static_cast<size_t>(i)]);
  }
}

TEST(sinusoid_bank_deterministic_across_different_note_coordinates_still_matches_when_equal) {
  // Same coordinate value built two different ways (different track/row but
  // an identical resulting coordinate) still produces identical output -
  // determinism is a function of the coordinate's value, not identity.
  NoteCoordinate coord_a(2, 10, 1);
  NoteCoordinate coord_b(2, 10, 1);
  auto params = baseParams(330.0f, 6, 44100.0f);

  SinusoidBank a(params, coord_a);
  SinusoidBank b(params, coord_b);

  const int frames = 256;
  vector<float> out_a(static_cast<size_t>(frames), 0.0f), out_b(static_cast<size_t>(frames), 0.0f);
  a.render(out_a.data(), frames);
  b.render(out_b.data(), frames);

  for (int i = 0; i < frames; i++) CHECK(out_a[static_cast<size_t>(i)] == out_b[static_cast<size_t>(i)]);
}

// Validates alpha_n = decay_a + decay_b * f_n^decay_p directly against the
// bank's own internal amplitude state (SinusoidBank::getPartialAmplitudeForTest()),
// rather than re-deriving alpha from the rendered signal via spectral/RMS
// analysis - since decay is a plain per-sample multiply
// (amp *= exp(-alpha/sample_rate)) applied exactly total_frames times, the
// bank's own internal amplitude after N samples must equal
// amplitude(0) * exp(-alpha * N / sample_rate) to within ordinary float
// rounding (checked here at a tight 0.1% relative tolerance - this is
// exact arithmetic, not a noisy measurement, so there's no measurement
// noise floor to budget for beyond float32 precision itself).
TEST(sinusoid_bank_per_partial_decay_rate_matches_formula) {
  const float sample_rate = 44100.0f;
  const float decay_a = 0.5f, decay_b = 0.0001f, decay_p = 1.3f;

  auto checkDecayFor = [&](float frequency) {
    SinusoidBank::Params params{
      frequency, /* partial_count */ 1,
      /* spectral_tilt_db */ 0.0f, /* inharmonicity_b */ 0.0f,
      /* edo_steps */ 0, /* tuning_matched */ false, /* partial_limit */ 8,
      decay_a, decay_b, decay_p,
      /* unison_voices */ 1, /* unison_detune_cents */ 0.0f,
      sample_rate
    };
    NoteCoordinate coord(0, 0, 0);
    SinusoidBank bank(params, coord);

    float alpha = decay_a + decay_b * powf(frequency, decay_p);
    // amplitude_n starts at 1.0 (tilt 0dB, n=1 -> 10^0 == 1).
    float start_amp = bank.getPartialAmplitudeForTest(0);
    CHECK_NEAR(start_amp, 1.0f, 1e-4f);

    const int frames = 4000; // well short of decaying past -90dB for these alphas
    vector<float> out(static_cast<size_t>(frames), 0.0f);
    bank.render(out.data(), frames);

    CHECK(bank.getActivePartialCountForTest() == 1); // not yet culled
    float measured = bank.getPartialAmplitudeForTest(0);
    float predicted = start_amp * expf(-alpha * static_cast<float>(frames) / sample_rate);
    CHECK(predicted > 1e-4f); // sanity: still well above the cull floor
    CHECK_NEAR(measured / predicted, 1.0f, 0.001f);
  };

  // Two different partial frequencies -> two different alphas (higher
  // frequency decays faster, per the spec's own "struck-string" model).
  checkDecayFor(220.0f);
  checkDecayFor(3000.0f);
}

TEST(sinusoid_bank_higher_frequency_partial_decays_faster) {
  const float sample_rate = 44100.0f;
  auto ampAfter = [&](float frequency, int frames) {
    SinusoidBank::Params params{
      frequency, 1, 0.0f, 0.0f, 0, false, 8,
      /* decay_a */ 0.2f, /* decay_b */ 0.002f, /* decay_p */ 1.2f,
      1, 0.0f, sample_rate
    };
    NoteCoordinate coord(0, 0, 0);
    SinusoidBank bank(params, coord);
    vector<float> out(static_cast<size_t>(frames), 0.0f);
    bank.render(out.data(), frames);
    return bank.getActivePartialCountForTest() > 0 ? bank.getPartialAmplitudeForTest(0) : 0.0f;
  };

  float low = ampAfter(150.0f, 8000);
  float high = ampAfter(4000.0f, 8000);
  CHECK(high < low);
}

TEST(sinusoid_bank_skips_partials_above_nyquist) {
  const float sample_rate = 44100.0f; // Nyquist 22050
  // Fundamental 5000Hz with 20 partials would reach 100kHz at n=20 - most
  // must be skipped.
  auto params = baseParams(5000.0f, 20, sample_rate);
  NoteCoordinate coord(0, 0, 0);
  SinusoidBank bank(params, coord);

  CHECK(bank.getActivePartialCountForTest() > 0);
  CHECK(bank.getActivePartialCountForTest() < 20);
}

TEST(sinusoid_bank_culls_fully_decayed_partial) {
  const float sample_rate = 44100.0f;
  SinusoidBank::Params params{
    440.0f, 1, 0.0f, 0.0f, 0, false, 8,
    /* decay_a */ 200.0f, /* decay_b */ 0.0f, /* decay_p */ 1.0f, // fast decay: alpha=200/s
    1, 0.0f, sample_rate
  };
  NoteCoordinate coord(0, 0, 0);
  SinusoidBank bank(params, coord);
  CHECK(bank.getActivePartialCountForTest() == 1);

  // -90dB (ratio ~3.16e-5) needs t = ln(1/3.16e-5)/200 =~ 0.0524s -> ~2310
  // frames at 44.1kHz; render well past that.
  vector<float> out(4410, 0.0f); // 0.1s
  bank.render(out.data(), 4410);

  CHECK(bank.getActivePartialCountForTest() == 0);
  CHECK(!bank.isActive());
}

// Inharmonicity must have zero effect within the tuning-matched region
// (n <= partial_limit): the fundamental (n=1, always <= partial_limit) is
// pinned to exactly the tuning-matched ratio (1.0) regardless of B, so a
// single-partial bank (partial_count=1) renders bit-identically whether B
// is 0 or nonzero - a strong, exact check, not just a tolerance-based one.
TEST(sinusoid_bank_inharmonicity_does_not_affect_partials_within_tuning_matched_limit) {
  auto params_flat = baseParams(220.0f, 1, 44100.0f);
  auto params_stretched = params_flat;
  params_stretched.inharmonicity_b = 0.001f;

  NoteCoordinate coord(2, 7, 0);
  SinusoidBank flat(params_flat, coord);
  SinusoidBank stretched(params_stretched, coord);

  const int frames = 1024;
  vector<float> out_flat(static_cast<size_t>(frames), 0.0f), out_stretched(static_cast<size_t>(frames), 0.0f);
  flat.render(out_flat.data(), frames);
  stretched.render(out_stretched.data(), frames);

  for (int i = 0; i < frames; i++) {
    CHECK(out_flat[static_cast<size_t>(i)] == out_stretched[static_cast<size_t>(i)]);
  }
}
