#include "TestFramework.h"

#include "../src/dsp/Metronome.h"

#include <cmath>
#include <vector>

TEST(metronome_click_starts_at_its_frame_and_is_silent_before) {
  dsp::Metronome m(48000);
  m.addClick(100, false);
  std::vector<float> out(512, 0.0f);
  m.render(out.data(), 512);
  for (int i = 0; i < 100; i++) CHECK(out[static_cast<size_t>(i)] == 0.0f);
  float peak = 0.0f;
  for (int i = 100; i < 512; i++) peak = std::max(peak, std::fabs(out[static_cast<size_t>(i)]));
  CHECK(peak > 0.1f);
}

TEST(metronome_click_carries_across_blocks_and_ends) {
  dsp::Metronome m(48000);
  m.addClick(60, true);
  std::vector<float> out(64, 0.0f);
  m.render(out.data(), 64);
  CHECK(m.isActive());
  std::vector<float> next(64, 0.0f);
  m.render(next.data(), 64);
  CHECK(next[0] != 0.0f);
  for (int i = 0; i < 100; i++) m.render(next.data(), 64);
  CHECK(!m.isActive());
}
