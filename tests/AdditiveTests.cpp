#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/GenericInstrument.h"
#include "../src/state/MemoryParameterSource.h"
#include <filesystem>
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <cmath>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif

namespace {
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
}

// Loads tests/fixtures/additive_note.xml (<envelope><additive
// preset="struck-string"/></envelope>, one note) through the same
// Song::open()/InstrumentProvider/renderSongOffline() path RenderTests.cpp
// uses for its own fixture-driven smoke tests, and asserts the render is
// finite and actually produces audible output - the basic "the new element
// is wired all the way through the real signal graph" check.
TEST(render_additive_struck_string_note_is_audible_and_finite) {
  InstrumentProvider provider; // no SoundFont: fixture only uses <additive>
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/additive_note.xml", provider);
  CHECK(ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(song, config);

  CHECK(!hasNonFiniteSample(result));
  CHECK(peakAbs(result) > 1e-4f);
}

TEST(generic_instrument_shows_the_soundfont_preset_name_not_its_path) {
  const char * font = "/usr/share/sounds/sf2/FluidR3_GM.sf2";
  if (!std::filesystem::exists(font)) return;
  InstrumentProvider provider;
  provider.loadSoundFont(font);
  MemoryParameterSource params;
  params.set("from", std::string("bass.electric.pick"));
  GenericInstrument instrument;
  instrument.loadParameters(params);
  instrument.prepare(provider);
  CHECK(instrument.getDisplayName() == "Picked Bass");
}
