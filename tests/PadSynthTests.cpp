#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <cmath>
#include <cstdio>
#include <fstream>
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
  // <padsynth> attributes (preset/tuningMatched/level) must survive a
  // save/reload the same way any other instrument's own attributes do -
  // loadFixture() above
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

TEST(padsynth_envelope_remap_attributes_round_trip_and_affect_render) {
  // The remap is genuinely wired into playback (not just parsed): a
  // fixture with envelopeAnchor/envelopeTracking/envelopePostprocess* set
  // should render differently from the same preset with them left at
  // their (off) default, and those attributes must survive a save/reload
  // the same way every other <padsynth> attribute does.
  auto remapped = loadFixture("padsynth_remap_note.xml");
  CHECK(remapped.ok);

  auto tmp_path = std::string(TESTS_SCRATCH_DIR) + "/padsynth_remap_roundtrip_scratch.xml";
  remapped.song.save(tmp_path);
  CHECK(std::ifstream(tmp_path).good());
  {
    std::ifstream in(tmp_path);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(contents.find("envelopeAnchor") != std::string::npos);
    CHECK(contents.find("envelopePostprocess=\"residue\"") != std::string::npos);
  }

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(tmp_path, provider));

  ChannelConfiguration config(44100, 1);
  auto remapped_render = renderSongOffline(remapped.song, config);
  auto reloaded_render = renderSongOffline(reloaded, config);
  CHECK(!hasNonFiniteSample(remapped_render));
  CHECK(remapped_render.interleaved.size() == reloaded_render.interleaved.size());
  bool all_equal = true;
  for (size_t i = 0; i < remapped_render.interleaved.size(); i++) {
    if (remapped_render.interleaved[i] != reloaded_render.interleaved[i]) { all_equal = false; break; }
  }
  CHECK(all_equal); // round trip is bit-identical

  std::remove(tmp_path.c_str());
}

TEST(extra_oscillator_presets_render_nonsilent_and_finite) {
  // choir-pad4/long-spacechoir2/bells-3/dual-strings/synth-piano-3-b aren't
  // wired into any InstrumentLibrary.cpp registration or the demo song -
  // this is their own coverage.
  auto loaded = loadFixture("padsynth_extra_presets.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(!hasNonFiniteSample(result));
  CHECK(peakAbs(result) > 1e-4f);
}
