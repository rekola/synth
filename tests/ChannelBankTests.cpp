#include "TestFramework.h"

#include "../src/dsp/Biquad.h"
#include "../src/dsp/ChannelBank.h"
#include "../src/dsp/MoogVCF.h"

#include <cmath>
#include <vector>

namespace {

float sampleAt(int slot, int i) {
  return 0.8f * sinf(0.05f * static_cast<float>(i) * static_cast<float>(slot + 1)) + 0.1f * static_cast<float>((i * 7 + slot * 3) % 5) - 0.2f;
}

}

TEST(transpose8x8_swaps_rows_and_columns) {
  dsp::v8f r[8];
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) r[i][j] = static_cast<float>(i * 8 + j);
  dsp::transpose8x8(r);
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) CHECK(r[i][j] == static_cast<float>(j * 8 + i));
}

TEST(transpose4x4_swaps_rows_and_columns) {
  dsp::v4d r[4];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) r[i][j] = static_cast<double>(i * 4 + j);
  dsp::transpose4x4(r);
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) CHECK(r[i][j] == static_cast<double>(j * 4 + i));
}

TEST(biquad_bank_matches_one_biquad_per_channel) {
  constexpr int kSlots = 18, kFrames = 64 + 5; // a block and a scalar tail
  const auto proto = Biquad<double>(FilterType::lowpass, 0.1, 0.9, 0.0);
  const auto c = proto.coefficients();
  dsp::BiquadBank bank(kSlots, { c.a0, c.a1, c.a2, c.b1, c.b2 });

  std::vector<Biquad<double>> reference(kSlots, Biquad<double>(FilterType::lowpass, 0.1, 0.9, 0.0));
  std::vector<std::vector<float>> data(kSlots, std::vector<float>(kFrames)), expected = data;

  for (int round = 0; round < 3; round++) { // state carries across calls
    for (int s = 0; s < kSlots; s++) {
      const bool silent = (s == 4 || (s == 17 && round == 1)); // a hole, and one that opens later
      for (int i = 0; i < kFrames; i++) {
        data[static_cast<size_t>(s)][static_cast<size_t>(i)] = silent ? 0.0f : sampleAt(s + round, i);
        expected[static_cast<size_t>(s)][static_cast<size_t>(i)] = data[static_cast<size_t>(s)][static_cast<size_t>(i)];
      }
      reference[static_cast<size_t>(s)].apply(kFrames, expected[static_cast<size_t>(s)].data());
      bank.plane(s) = silent ? nullptr : data[static_cast<size_t>(s)].data();
    }
    bank.apply(kFrames);
    for (int s = 0; s < kSlots; s++) {
      const bool silent = (s == 4 || (s == 17 && round == 1));
      if (silent) continue; // nothing is written back to a slot with no signal
      for (int i = 0; i < kFrames; i++) CHECK_NEAR(data[static_cast<size_t>(s)][static_cast<size_t>(i)], expected[static_cast<size_t>(s)][static_cast<size_t>(i)], 1e-5f);
    }
  }
}

TEST(moog_bank_matches_one_filter_per_channel) {
  constexpr int kSlots = 11, kFrames = 64 + 3;
  dsp::MoogBank bank(kSlots);
  std::vector<MoogVCF<float>> reference(kSlots);
  std::vector<std::vector<float>> data(kSlots, std::vector<float>(kFrames)), expected = data;

  for (int round = 0; round < 3; round++) {
    for (int s = 0; s < kSlots; s++) {
      const bool silent = (s == 2 || (s == 10 && round == 2));
      for (int i = 0; i < kFrames; i++) {
        data[static_cast<size_t>(s)][static_cast<size_t>(i)] = silent ? 0.0f : sampleAt(s + round, i);
        expected[static_cast<size_t>(s)][static_cast<size_t>(i)] = data[static_cast<size_t>(s)][static_cast<size_t>(i)];
      }
      if (silent) reference[static_cast<size_t>(s)].apply(kFrames, 0.3f, 1.0f);
      else reference[static_cast<size_t>(s)].apply(kFrames, expected[static_cast<size_t>(s)].data(), 0.3f, 1.0f);
      bank.plane(s) = silent ? nullptr : data[static_cast<size_t>(s)].data();
    }
    bank.apply(kFrames, 0.3f, 1.0f);
    for (int s = 0; s < kSlots; s++) {
      if (s == 2 || (s == 10 && round == 2)) continue;
      for (int i = 0; i < kFrames; i++) CHECK_NEAR(data[static_cast<size_t>(s)][static_cast<size_t>(i)], expected[static_cast<size_t>(s)][static_cast<size_t>(i)], 1e-4f);
    }
  }
}
