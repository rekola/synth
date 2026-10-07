#include "TestFramework.h"

#include "../src/model/PercussionTrack.h"
#include "../src/model/Song.h"
#include "../src/model/Pattern.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/SoundFont.h"
#include "Sf2Fixture.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"

#include <filesystem>
#include <cmath>
#include <algorithm>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

TEST(percussion_track_appears_in_get_root_track_ids) {
  // Song::getRootTrackIds() is the shared source of "which tracks are
  // columns" for the tracker view, LaunchpadManager, and UI.
  Song song;
  auto & track = song.addTrack(make_unique<PercussionTrack>());
  auto ids = song.getRootTrackIds();
  CHECK(ids.size() == 2);
  CHECK(ids[0] == track.getInternalId());
  CHECK(ids[1] == song.getMasterTrack().getInternalId());
}

// --- save/load round trip ---

TEST(percussion_track_round_trips_through_save_and_load) {
  namespace fs = std::filesystem;
  auto scratch_path = (fs::path(TESTS_SCRATCH_DIR) / "percussion_track_round_trip_scratch.xml").string();

  Song song;
  song.addTrack(make_unique<PercussionTrack>());
  song.getArrangement();
  song.save(scratch_path);

  InstrumentProvider provider;
  Song reloaded;
  CHECK(reloaded.open(scratch_path, provider));

  CHECK(reloaded.getMasterTrack().getChildren().size() == 1);
  auto & reloaded_track = *reloaded.getMasterTrack().getChildren()[0];
  CHECK(reloaded_track.getElementName() == std::string("percussionTrack"));
  CHECK(reloaded_track.getType() == TrackType::PERCUSSION_CONTROL);
  auto * percussion_track = dynamic_cast<PercussionTrack *>(&reloaded_track);
  CHECK(percussion_track != nullptr);

  fs::remove(scratch_path);
}

// --- getHitNotesForRow() itself - pure, no audio engine needed ---

// --- SongState wiring - real audio, via a fixture ---

namespace {

// Renders exactly one row's worth of samples and returns the peak absolute
// sample value across every output channel - "was anything audible in
// this row" without needing bit-exact waveform comparison (which would be
// fragile here: playNote()'s start_phase argument is randomized per note-on,
// see InstrumentTrackState.h).
float renderRowPeak(SongState & state, const Song & song, Mixer & mixer, int row_samples) {
  state.renderBlock(row_samples, song, mixer);
  auto master = mixer.encode();
  float peak = 0.0f;
  for (int c = 0; c < mixer.getOutChannels(); c++) {
    auto data = master.getChannelData(c);
    for (int i = 0; i < row_samples; i++) peak = std::max(peak, std::fabs(data[i]));
  }
  return peak;
}

constexpr float kAudiblePeak = 1e-3f;
constexpr float kSilentPeak = 1e-4f;

// A PercussionTrack has no per-track instrument_id_ of its own - every
// note plays through the pool's one default kit
// (InstrumentPool::getDefaultKitInstrument(), resolved from `provider` via
// the song's <instruments from="..."> - see Song::open()). None of these
// fixtures set that attribute, so it defaults to path "kit" - registering
// a fast-decaying synthetic SF2 preset directly under that literal key
// (registerPath(), same call InstrumentProvider::resolvePath() ends up
// walking to) makes song.open() resolve it exactly the same way a real
// kit.* SoundFont path would, without needing an actual GM font on disk.
// SF2, not a plain Oscillator or an <envelope> wrapper: registerPath()
// only accepts a shared_ptr<Instrument>, and only SoundFontInstrument (via
// SoundFont::createInstrument()) gives volume-envelope decay without
// needing an Effect wrapper. Needed only by the tests below that count
// audible rows over time (decay matters); every other fixture in this
// file just checks whether a note fires at all, which doesn't depend on
// how it decays.
void registerFastDecayKit(InstrumentProvider & provider) {
  using namespace sf2fixture;
  // Timecents = 1200*log2(seconds) - GeneratorOverrideTests.cpp's own
  // DecayVolEnv=2400.0f/"4s base decay time" data point confirms the
  // scale (2^(2400/1200) = 4). ~1ms attack, ~5ms hold, ~5ms decay to
  // silence, comfortably inside one row - fast enough that a hit row is
  // loud and every row after it is back below kSilentPeak.
  std::vector<PresetSpec> presets = {
    { "FastDecayKit", 0, {
      GenSpec{ 34, -12000 }, // AttackVolEnv ~ 1ms
      GenSpec{ 35, -9200 },  // HoldVolEnv ~ 5ms
      GenSpec{ 36, -9200 },  // DecayVolEnv ~ 5ms
      GenSpec{ 37, 1000 },   // SustainVolEnv ~ -100dB, effectively silent
      GenSpec{ 38, -9200 },  // ReleaseVolEnv ~ 5ms - never actually reached (no note-off)
    } },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "drum_machine_fast_decay_kit_fixture.sf2").string();
  writeMinimalSf2(path, presets);
  // SoundFont sf(path) itself need not outlive this call - the returned
  // Instrument holds its own shared_ptr<SoundFontFile> (SoundFontInstrument's
  // own ctor argument, SoundFont.cpp's createInstrument()), independent of
  // the SoundFont wrapper object's own lifetime.
  SoundFont sf(path);
  provider.registerPath("kit", sf.createInstrument(0));
}

} // namespace

TEST(percussion_track_seek_directly_to_a_later_repetition_still_triggers_the_right_hits) {
  // The actual risk this whole feature is about: seeking straight to a row
  // deep into a loop - never having rendered any row before it - must
  // still compute the correct hits for that row. Two entirely independent
  // SongState instances, neither of which ever renders row 0..23, so
  // there is no possible "warm-up" history to lean on.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/percussion_track_32rows.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());

  {
    // Row 24 (24 % 8 == 0): BD and CH both hit per the fixture's pattern.
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    state.setPosition(24);
    CHECK(renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak);
  }
  {
    // Row 25 (25 % 8 == 1): no lane has a step lit at index 1.
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    state.setPosition(25);
    CHECK(renderRowPeak(state, song, *mixer, row_samples) < kSilentPeak);
  }
}

TEST(percussion_track_loop_truncates_at_the_end_of_a_short_pattern) {
  // An 8-row pattern repeated through row 19, one lane hit only at step 4
  // - hits land at rows 4 and 12; a would-be third repetition at row 20
  // never happens because the song ends at row 19. Exactly 2 audible
  // onsets, not 3.
  InstrumentProvider provider;
  registerFastDecayKit(provider);
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/percussion_track_20rows.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int hits = 0;
  for (int row = 0; row < 20; row++) {
    if (renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak) hits++;
  }
  CHECK(hits == 2);
}

TEST(percussion_track_plays_each_row_of_its_pattern_once) {
  // BD at row 0 and SD at row 6, with nothing in between - both sound, at
  // their own rows, and nothing else does.
  InstrumentProvider provider;
  registerFastDecayKit(provider);
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/percussion_two_patterns.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int hits = 0;
  for (int row = 0; row < 7; row++) {
    if (renderRowPeak(state, song, *mixer, row_samples) > kAudiblePeak) hits++;
  }
  CHECK(hits == 2); // row 0's BD and row 6's SD
}

TEST(percussion_track_retrigger_chokes_the_previous_hit_instead_of_stacking) {
  // Lane BD hits at rows 0 and 8 (pattern length 8, one step lit), with a
  // long (2s) release and nonzero sustain - a hit only note-on's, never
  // note-off's, so voice #1 would sit audibly at its sustain level
  // forever if retriggerVoices() didn't force a fast release on it when
  // the second hit lands.
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(std::string(TESTS_FIXTURES_DIR) + "/percussion_retrigger.xml", provider));

  ChannelConfiguration config(44100, 1);
  int row_samples = config.getSampleInterval(song.getTempo());
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  for (int row = 0; row < 8; row++) state.renderBlock(row_samples, song, *mixer); // rows 0..7
  // getVoiceCount() counts the whole active node tree (this track's own
  // state node plus each voice's envelope/oscillator wrapper chain), not
  // "number of notes" - so the right baseline to compare against is
  // whatever one ringing voice's own tree measures as, not a hardcoded
  // constant tied to this fixture's specific instrument chain depth.
  int one_voice_worth = state.getVoiceCount();
  CHECK(one_voice_worth > 0);

  state.renderBlock(row_samples, song, *mixer); // row 8 - second hit, must choke voice #1
  // A little more than the ~10ms fast-release window, comfortably less
  // than one row's own 125ms.
  state.renderBlock(static_cast<int>(0.05f * config.getAudioOutSampleRate()), song, *mixer);

  // Back to exactly one voice's worth - voice #1 was choked and cleaned
  // up, not left ringing forever alongside voice #2 (which is what an
  // unbounded doubling towards two voices' worth would mean here).
  CHECK(state.getVoiceCount() == one_voice_worth);
}

