#include "TestFramework.h"

#include "../src/instruments/FMIndexDecay.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/model/Song.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <cmath>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif

using namespace std;

namespace {

bool render(const char * name, OfflineRenderResult & out) {
  InstrumentProvider provider;
  Song song;
  if (!song.open(string(TESTS_FIXTURES_DIR) + "/" + name, provider)) return false;
  ChannelConfiguration config(44100, 1);
  out = renderSongOffline(song, config);
  return true;
}

// Mean absolute sample-to-sample step relative to the mean absolute level:
// larger for a signal with more high-frequency content.
float brightness(const OfflineRenderResult & r) {
  double level = 0.0, step = 0.0;
  size_t stride = static_cast<size_t>(r.channels);
  for (size_t i = 1; i < r.numberOfFrames(); i++) {
    level += std::fabs(r.interleaved[i * stride]);
    step += std::fabs(r.interleaved[i * stride] - r.interleaved[(i - 1) * stride]);
  }
  return level > 0.0 ? static_cast<float>(step / level) : 0.0f;
}

}

TEST(fm_with_zero_index_is_a_plain_sine) {
  OfflineRenderResult fm, sine;
  CHECK(render("fm_index_zero.xml", fm));
  CHECK(render("fm_reference_sine.xml", sine));
  CHECK(fm.numberOfFrames() == sine.numberOfFrames());
  for (size_t i = 0; i < fm.interleaved.size(); i++) {
    CHECK_NEAR(fm.interleaved[i], sine.interleaved[i], 1e-4f);
  }
}

TEST(fm_index_adds_sidebands) {
  OfflineRenderResult plain, modulated;
  CHECK(render("fm_index_zero.xml", plain));
  CHECK(render("fm_index_high.xml", modulated));
  for (auto v : modulated.interleaved) CHECK(std::isfinite(v));
  CHECK(brightness(modulated) > brightness(plain) * 1.5f);
}

TEST(a_leaf_instrument_with_children_fails_to_load) {
  OfflineRenderResult unused;
  CHECK(!render("oscillator_with_child.xml", unused));
}

TEST(fm_index_decay_reaches_exactly_zero_without_denormals) {
  FMIndexDecay decay(4.0f, 0.06f, 44100.0f);
  bool reached_zero = false;
  for (int i = 0; i < 44100 * 30; i++) {
    decay.advance();
    CHECK(std::fpclassify(decay.value()) != FP_SUBNORMAL);
    if (decay.value() == 0.0f) reached_zero = true;
  }
  CHECK(reached_zero);
  CHECK(decay.value() == 0.0f);
}

TEST(fm_index_decay_with_zero_time_constant_stays_constant) {
  FMIndexDecay decay(2.0f, 0.0f, 44100.0f);
  for (int i = 0; i < 44100; i++) decay.advance();
  CHECK(decay.value() == 2.0f);
}

TEST(fm_index_decay_follows_the_time_constant) {
  FMIndexDecay decay(2.0f, 0.5f, 44100.0f);
  for (int i = 0; i < 22050; i++) decay.advance(); // one time constant
  CHECK_NEAR(decay.value(), 2.0f * std::exp(-1.0f), 1e-3f);
}

TEST(fm_detune_shifts_pitch_by_cents) {
  OfflineRenderResult plain, octave;
  CHECK(render("fm_index_zero.xml", plain));
  CHECK(render("fm_detune_octave.xml", octave));
  // Zero crossings while the fixtures' envelope holds at full level.
  auto crossings = [](const OfflineRenderResult & r) {
    size_t stride = static_cast<size_t>(r.channels), count = 0;
    for (size_t i = 1000; i < 13000 && i < r.numberOfFrames(); i++) {
      if ((r.interleaved[(i - 1) * stride] < 0.0f) != (r.interleaved[i * stride] < 0.0f)) count++;
    }
    return static_cast<float>(count);
  };
  CHECK_NEAR(crossings(octave) / crossings(plain), 2.0f, 0.02f);
}

TEST(fm_index_tracking_brightens_low_notes) {
  OfflineRenderResult fixed, tracked;
  CHECK(render("fm_tracking_0.xml", fixed));
  CHECK(render("fm_tracking_1.xml", tracked));
  CHECK(brightness(tracked) > brightness(fixed) * 1.5f);
}

TEST(fm_feedback_brightens_the_modulator) {
  OfflineRenderResult plain, fed_back;
  CHECK(render("fm_tracking_0.xml", plain));
  CHECK(render("fm_feedback.xml", fed_back));
  for (auto v : fed_back.interleaved) CHECK(std::isfinite(v));
  CHECK(brightness(fed_back) > brightness(plain) * 1.2f);
}
