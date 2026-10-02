#include "TestFramework.h"

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
