#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <cmath>
#include <cstdio>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

namespace {

struct Loaded {
  bool ok;
  Song song;
};

// Same shape as RenderTests.cpp's own loadFixture() (no SoundFont needed -
// this fixture only uses the built-in padsynth oscillator).
Loaded loadFixture(const char * name) {
  InstrumentProvider provider;
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/" + name, provider);
  return { ok, std::move(song) };
}

bool hasNonFiniteSample(const OfflineRenderResult & result) {
  for (auto v : result.interleaved) {
    if (!std::isfinite(v)) return true;
  }
  return false;
}

float peakAbs(const OfflineRenderResult & result) {
  float peak = 0.0f;
  for (auto v : result.interleaved) peak = std::max(peak, std::fabs(v));
  return peak;
}

} // namespace

TEST(padsynth_note_renders_nonsilent_and_finite) {
  auto loaded = loadFixture("padsynth_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(!hasNonFiniteSample(result));
  CHECK(peakAbs(result) > 1e-4f);
}

TEST(padsynth_element_round_trips_through_save_and_load) {
  // <padsynth> attributes (preset/bandwidth/bandwidthScale/partials/
  // partialLimit/tuningMatched/level) must survive a save/reload the same
  // way any other instrument's own attributes do - loadFixture() above
  // only exercises the load half.
  auto loaded = loadFixture("padsynth_note.xml");
  CHECK(loaded.ok);

  auto tmp_path = std::string(TESTS_SCRATCH_DIR) + "/padsynth_roundtrip_scratch.xml";
  loaded.song.save(tmp_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(tmp_path, provider));

  ChannelConfiguration config(44100, 1);
  auto original_render = renderSongOffline(loaded.song, config);
  auto reloaded_render = renderSongOffline(reloaded, config);

  CHECK(!hasNonFiniteSample(reloaded_render));
  CHECK(original_render.interleaved.size() == reloaded_render.interleaved.size());
  bool all_equal = true;
  for (size_t i = 0; i < original_render.interleaved.size(); i++) {
    if (original_render.interleaved[i] != reloaded_render.interleaved[i]) { all_equal = false; break; }
  }
  CHECK(all_equal);

  std::remove(tmp_path.c_str());
}
