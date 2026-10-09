#include "TestFramework.h"

#include "../src/instruments/SinusoidBank.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {
constexpr float kRate = 44100.0f;

PartialSpec spec(float frequency, float amplitude = 1.0f, float alpha = 0.2f, float phase = 0.3f, int group = 0) {
  return PartialSpec{frequency, amplitude, alpha, phase, group};
}

vector<float> renderRows(SinusoidBank & bank, int rows, int frames) {
  vector<float> out(static_cast<size_t>(rows * frames), 0.0f);
  bank.render(out.data(), static_cast<size_t>(frames), frames);
  return out;
}
}

TEST(sinusoid_bank_is_deterministic) {
  vector<PartialSpec> specs{spec(220.0f), spec(440.0f, 0.5f, 0.4f, 1.1f), spec(660.0f, 0.3f, 0.6f, 2.0f)};
  SinusoidBank a(specs, kRate), b(specs, kRate);
  auto out_a = renderRows(a, 1, 512), out_b = renderRows(b, 1, 512);
  for (size_t i = 0; i < out_a.size(); i++) CHECK(out_a[i] == out_b[i]);
}

// A partial's amplitude after N samples must equal amplitude(0) *
// exp(-alpha * N / sample_rate): plain per-sample multiplication, so float
// rounding is the only error.
TEST(sinusoid_bank_per_partial_decay_rate_matches_alpha) {
  for (float alpha : {0.5f, 3.0f}) {
    SinusoidBank bank({spec(220.0f, 1.0f, alpha)}, kRate);
    CHECK_NEAR(bank.getPartialAmplitudeForTest(0), 1.0f, 1e-6f);
    const int frames = 4000;
    renderRows(bank, 1, frames);
    CHECK(bank.getActivePartialCountForTest() == 1);
    float predicted = expf(-alpha * static_cast<float>(frames) / kRate);
    CHECK_NEAR(bank.getPartialAmplitudeForTest(0) / predicted, 1.0f, 0.001f);
  }
}

TEST(sinusoid_bank_skips_partials_above_nyquist) {
  vector<PartialSpec> specs;
  for (int n = 1; n <= 20; n++) specs.push_back(spec(5000.0f * static_cast<float>(n)));
  SinusoidBank bank(specs, kRate); // Nyquist 22050: only 5000, 10000, 15000, 20000 fit
  CHECK(bank.getActivePartialCountForTest() == 4);
}

TEST(sinusoid_bank_culls_fully_decayed_partial) {
  SinusoidBank bank({spec(440.0f, 1.0f, 200.0f)}, kRate);
  CHECK(bank.getActivePartialCountForTest() == 1);
  // -90 dB needs ln(1/3.16e-5)/200 = 0.052 s; render 0.1 s.
  renderRows(bank, 1, 4410);
  CHECK(bank.getActivePartialCountForTest() == 0);
  CHECK(!bank.isActive());
}

TEST(sinusoid_bank_cull_keeps_the_other_partials) {
  SinusoidBank bank({spec(220.0f, 1.0f, 0.2f), spec(440.0f, 1.0f, 400.0f), spec(660.0f, 1.0f, 0.2f)}, kRate);
  renderRows(bank, 1, 4410);
  CHECK(bank.getActivePartialCountForTest() == 2);
  float expected = expf(-0.2f * 4410.0f / kRate);
  CHECK_NEAR(bank.getPartialAmplitudeForTest(0) / expected, 1.0f, 0.001f);
  CHECK_NEAR(bank.getPartialAmplitudeForTest(1) / expected, 1.0f, 0.001f);
}

// Groups only route: the rows added together equal one row holding the same
// partials.
TEST(sinusoid_bank_groups_sum_to_the_ungrouped_output) {
  vector<PartialSpec> grouped{spec(220.0f, 1.0f, 0.3f, 0.1f, 0), spec(330.0f, 0.5f, 0.4f, 0.7f, 1),
                              spec(440.0f, 0.7f, 0.5f, 1.3f, 2), spec(550.0f, 0.2f, 0.6f, 2.1f, 1)};
  auto flat = grouped;
  for (auto & p : flat) p.group = 0;

  const int frames = 700;
  SinusoidBank g(grouped, kRate), f(flat, kRate);
  CHECK(g.groupCount() == 3);
  auto rows = renderRows(g, 3, frames);
  auto single = renderRows(f, 1, frames);
  for (int i = 0; i < frames; i++) {
    size_t k = static_cast<size_t>(i);
    float sum = rows[k] + rows[static_cast<size_t>(frames) + k] + rows[2 * static_cast<size_t>(frames) + k];
    CHECK_NEAR(sum, single[k], 1e-4f);
  }
}

TEST(sinusoid_bank_a_group_writes_only_its_own_row) {
  SinusoidBank bank({spec(220.0f, 1.0f, 0.3f, 0.5f, 1)}, kRate);
  const int frames = 256;
  auto rows = renderRows(bank, 2, frames);
  float row0 = 0.0f, row1 = 0.0f;
  for (int i = 0; i < frames; i++) {
    row0 += fabsf(rows[static_cast<size_t>(i)]);
    row1 += fabsf(rows[static_cast<size_t>(frames + i)]);
  }
  CHECK(row0 == 0.0f);
  CHECK(row1 > 1.0f);
}
