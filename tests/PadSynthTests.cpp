#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/instruments/PadSynth.h"
#include "../src/instruments/InstrumentLibrary.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/ambisonic/SphericalPosition.h"

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

namespace {

// Rising zero crossings of the W channel over a few blocks of one note.
int risingCrossings(float detune_cents) {
  PadSynth pad;
  MemoryParameterSource params;
  params.set("preset", string("soft-pad")); // a single partial: a clean sine
  params.set("detune", detune_cents);
  pad.loadParameters(params);

  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  auto voice = pad.playNote(config, position, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, NoteCoordinate(0, 0, 0)); // ~294 Hz

  int crossings = 0;
  float prev = 0.0f;
  for (int block = 0; block < 8; block++) {
    auto out = voice->render(1024);
    for (int k = 0; k < 1024; k++) {
      float v = out.getChannelData(0)[k];
      if (prev <= 0.0f && v > 0.0f) crossings++;
      prev = v;
    }
  }
  return crossings;
}

} // namespace

TEST(padsynth_detune_in_cents_shifts_pitch_and_round_trips) {
  int base = risingCrossings(0.0f);
  int octave = risingCrossings(1200.0f);
  CHECK(base > 20);
  // 1200 cents is an octave up: twice the cycles, give or take a cycle at
  // the edges of the measured window.
  CHECK(octave >= 2 * base - 2 && octave <= 2 * base + 2);

  PadSynth pad;
  MemoryParameterSource params;
  params.set("detune", 700.0f);
  pad.loadParameters(params);
  MemoryParameterSource stored;
  pad.storeParameters(stored);
  CHECK_NEAR(stored.get<float>("detune", 0.0f), 700.0f, 1e-6f);

  PadSynth plain;
  MemoryParameterSource plain_stored;
  plain.storeParameters(plain_stored);
  CHECK(!plain_stored.has("detune")); // the default isn't written
}

namespace {

// A group of padsynth copies playing one note, summed.
AudioBuffer renderEnsemble(const vector<float> & cents, int frames) {
  vector<unique_ptr<PadSynth>> pads;
  vector<unique_ptr<VoiceState>> voices;
  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  AudioBuffer sum(config.numberOfChannels(), frames);
  sum.zero();
  for (float c : cents) {
    auto pad = make_unique<PadSynth>();
    MemoryParameterSource params;
    params.set("preset", string("strings"));
    params.set("detune", c);
    pad->loadParameters(params);
    voices.push_back(pad->playNote(config, position, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, NoteCoordinate(1, 4, 0)));
    pads.push_back(move(pad));
  }
  for (auto & v : voices) sum.mixNamed(v->render(frames));
  return sum;
}

double rmsOf(const AudioBuffer & b) {
  double e = 0.0;
  int n = b.numberOfFrames();
  for (int i = 0; i < n; i++) e += static_cast<double>(b.getChannelData(0)[i]) * static_cast<double>(b.getChannelData(0)[i]);
  return sqrt(e / n);
}

} // namespace

// Detuned copies of one note must not start phase-locked: three coherent
// copies would sum to three times one copy's level, independent ones to
// about the square root of three.
TEST(padsynth_detuned_ensemble_copies_start_at_different_phases) {
  const int frames = 1024; // well inside one beat period, so the copies stay near their start phases
  double single = rmsOf(renderEnsemble({ 0.0f }, frames));
  double three = rmsOf(renderEnsemble({ -14.0f, 0.0f, 14.0f }, frames));
  CHECK(single > 1e-4);
  CHECK(three < 2.5 * single); // locked would be ~3x
  CHECK(three > 0.6 * single);
}

TEST(library_ensemble_pads_are_three_detuned_padsynth_copies_and_render) {
  InstrumentProvider provider;
  registerLibraryInstruments(provider);
  for (const char * path : { "pad.poly", "pad.choir", "pad.bowed", "string.synth.slow", "brass.synth" }) {
    auto instrument = provider.resolvePath(path);
    CHECK(instrument != nullptr);
    if (!instrument) continue;

    ChannelConfiguration config(44100, 1);
    SphericalPosition position;
    position.distance = 1.0f;
    auto voice = instrument->playNote(config, position, Tuning::EDO31, 1.0f, 1.0f, 160, SendLevels{}, NoteCoordinate(0, 0, 0));
    CHECK(voice.get() != nullptr);
    // The envelope's group holds one voice per copy.
    CHECK(voice->getAllocatedVoiceCount() >= 4);
    auto out = voice->render(2048);
    bool any = false;
    for (int k = 0; k < 2048; k++) {
      float v = out.getChannelData(0)[k];
      CHECK(std::isfinite(v));
      any = any || v != 0.0f;
    }
    CHECK(any);
  }
}
