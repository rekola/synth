#include "TestFramework.h"
#include "Sf2Fixture.h"

#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/InstrumentLibrary.h"
#include "../src/effects/EnvelopeFilter.h"
#include "../src/effects/TapeDegradation.h"
#include "../src/model/Song.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <cmath>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

namespace {

float rms(const OfflineRenderResult & result, int channel) {
  double sum = 0.0;
  auto frames = result.numberOfFrames();
  for (size_t i = 0; i < frames; i++) sum += std::pow(result.interleaved[i * static_cast<size_t>(result.channels) + static_cast<size_t>(channel)], 2);
  return frames ? static_cast<float>(std::sqrt(sum / frames)) : 0.0f;
}

bool hasNonFiniteSample(const OfflineRenderResult & result) {
  for (auto sample : result.interleaved) if (!std::isfinite(sample)) return true;
  return false;
}

// registerLibraryInstruments() runs once at Controller startup, after
// loadSoundFont() - fixtures below reproduce exactly that order, since
// InstrumentProvider itself never calls it implicitly (a bare provider,
// e.g. RenderTests.cpp's loadFixture(), stays library-free on purpose).
bool renderLibraryFixture(const char * name, InstrumentProvider & provider, OfflineRenderResult & out) {
  Song song;
  if (!song.open(std::string(TESTS_FIXTURES_DIR) + "/" + name, provider)) return false;
  ChannelConfiguration config(44100, 1);
  out = renderSongOffline(song, config);
  return true;
}

}

// GM synth pads (89-96) -----------------------------------------------

TEST(gm_pad_path_resolves_to_envelope_padsynth) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);

  auto resolved = provider.resolvePath("pad.warm");
  CHECK(resolved != nullptr);
  CHECK(dynamic_cast<EnvelopeFilter *>(resolved.get()) != nullptr);
}

TEST(gm_pad_override_takes_priority_over_an_existing_soundfont_pad) {
  std::string path = std::string(TESTS_SCRATCH_DIR) + "/instrument_library_pad.sf2";
  // Program 89 (0-indexed) = GM Program 90 = "Pad 2 (warm)" = pad.warm,
  // per GmInstrumentTable.h.
  sf2fixture::writeMinimalSf2(path, { {"Warm Pad", 89, {}, {}} });

  InstrumentProvider provider;
  provider.loadSoundFont(path);
  // Before the library registers its own override, the SF2 preset is what
  // resolves - confirms the fixture itself actually registered something
  // at pad.warm, so the next check is a real override, not a no-op path.
  CHECK(dynamic_cast<EnvelopeFilter *>(provider.resolvePath("pad.warm").get()) == nullptr);

  registerLibraryInstruments(provider);
  CHECK(dynamic_cast<EnvelopeFilter *>(provider.resolvePath("pad.warm").get()) != nullptr);
}

TEST(gm_pad_render_is_non_silent_and_finite) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);

  OfflineRenderResult result;
  CHECK(renderLibraryFixture("library_pad_warm.xml", provider, result));
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-5f);
}

// Additive piano ---------------------------------------------------------

TEST(additive_piano_is_fallback_when_soundfont_has_no_piano) {
  InstrumentProvider provider; // no loadSoundFont() at all
  registerLibraryInstruments(provider);

  auto resolved = provider.resolvePath("piano.acoustic.grand");
  CHECK(resolved != nullptr);
  CHECK(dynamic_cast<EnvelopeFilter *>(resolved.get()) != nullptr);
}

TEST(additive_piano_does_not_override_an_existing_soundfont_piano) {
  std::string path = std::string(TESTS_SCRATCH_DIR) + "/instrument_library_piano.sf2";
  sf2fixture::writeMinimalSf2(path, { {"Grand Piano", 0, {}, {}} }); // program 0 = piano.acoustic.grand

  InstrumentProvider provider;
  provider.loadSoundFont(path);
  registerLibraryInstruments(provider);

  auto resolved = provider.resolvePath("piano.acoustic.grand");
  CHECK(resolved != nullptr);
  CHECK(dynamic_cast<EnvelopeFilter *>(resolved.get()) == nullptr); // still the real SF2 piano
}

TEST(additive_piano_always_available_at_its_own_path_for_comparison) {
  std::string path = std::string(TESTS_SCRATCH_DIR) + "/instrument_library_piano2.sf2";
  sf2fixture::writeMinimalSf2(path, { {"Grand Piano", 0, {}, {}} });

  InstrumentProvider provider;
  provider.loadSoundFont(path);
  registerLibraryInstruments(provider);

  // piano.acoustic.grand keeps the real SF2 piano (previous test), but
  // piano.additive is always the additive one, forceable for comparison.
  auto resolved = provider.resolvePath("piano.additive");
  CHECK(resolved != nullptr);
  CHECK(dynamic_cast<EnvelopeFilter *>(resolved.get()) != nullptr);
}

TEST(additive_piano_render_is_non_silent_and_finite) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);

  OfflineRenderResult result;
  CHECK(renderLibraryFixture("library_additive_piano.xml", provider, result));
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-5f);
}

// Mellotron ----------------------------------------------------------------

TEST(mellotron_is_registered_as_tape_wrapping_envelope_padsynth) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);

  auto resolved = provider.resolvePath("keyboard.tape.mellotron");
  CHECK(resolved != nullptr);
  CHECK(dynamic_cast<TapeDegradation *>(resolved.get()) != nullptr);
}

TEST(mellotron_render_is_non_silent_and_finite) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);

  OfflineRenderResult result;
  CHECK(renderLibraryFixture("library_mellotron.xml", provider, result));
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-5f);
}
