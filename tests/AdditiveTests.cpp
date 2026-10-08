#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/GenericInstrument.h"
#include "../src/state/MemoryParameterSource.h"
#include <filesystem>
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include "../src/instruments/Additive.h"
#include "../src/instruments/Tuning.h"
#include "../src/model/SendLevels.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

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

// Timing of the <additive> voice chain (bank plus ambisonic encode), printed
// to stderr and never asserted on. Runs only with SYNTH_TIMING set;
// SYNTH_TIMING_PRESET picks the preset (default "piano").
TEST(additive_voice_timing) {
  if (!std::getenv("SYNTH_TIMING")) return;
  const char * preset_env = std::getenv("SYNTH_TIMING_PRESET");
  std::string preset = preset_env ? preset_env : "piano";

  Additive additive;
  MemoryParameterSource params;
  params.set("preset", preset);
  additive.loadParameters(params);

  ChannelConfiguration config(48000, 3);
  SphericalPosition position;
  position.distance = 1.0f;
  SendLevels sends;
  constexpr int kFrames = 1024, kVoices = 32, kRounds = 20;
  auto now = []() { return std::chrono::steady_clock::now(); };
  auto micros = [](auto a, auto b) { return std::chrono::duration<double, std::micro>(b - a).count(); };

  // One fresh voice: note-on construction, then the first block.
  double on_us = 0.0, first_us = 0.0;
  for (int r = 0; r < kRounds; r++) {
    auto t0 = now();
    auto voice = additive.playNote(config, position, Tuning::EDO12, 1.0f, 1.0f, 60, sends, NoteCoordinate(0, r, 0));
    auto t1 = now();
    voice->render(kFrames);
    auto t2 = now();
    on_us += micros(t0, t1);
    first_us += micros(t1, t2);
  }

  // 32 simultaneous voices spread over the keyboard: first block, then the
  // block 0.5 s in (about 23 blocks later).
  double chord_on_us = 0.0, chord_first_us = 0.0, chord_later_us = 0.0;
  for (int r = 0; r < kRounds; r++) {
    std::vector<std::unique_ptr<VoiceState>> voices;
    auto t0 = now();
    for (int v = 0; v < kVoices; v++) {
      voices.push_back(additive.playNote(config, position, Tuning::EDO12, 1.0f, 1.0f, 36 + v * 2, sends, NoteCoordinate(0, r, v)));
    }
    auto t1 = now();
    for (auto & voice : voices) voice->render(kFrames);
    auto t2 = now();
    for (int b = 0; b < 22; b++)
      for (auto & voice : voices) voice->render(kFrames);
    auto t3 = now();
    for (auto & voice : voices) voice->render(kFrames);
    auto t4 = now();
    chord_on_us += micros(t0, t1);
    chord_first_us += micros(t1, t2);
    chord_later_us += micros(t3, t4);
    (void)t3;
  }

  std::fprintf(stderr,
               "additive timing (%s, %d frames @48 kHz, budget %.0f us/block):\n"
               "  one voice: note-on %.1f us, first block %.1f us\n"
               "  %d voices: note-on %.1f us total, first block %.1f us total, block 24 %.1f us total\n",
               preset.c_str(), kFrames, kFrames / 48000.0 * 1e6,
               on_us / kRounds, first_us / kRounds,
               kVoices, chord_on_us / kRounds, chord_first_us / kRounds, chord_later_us / kRounds);
}
