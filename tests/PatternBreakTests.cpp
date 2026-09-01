#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/effects/Amplifier.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

using namespace std;

// ZBxx is the first pattern effect command with real playback semantics
// (every other one in docs/commands.md is still a stub - see SongState::
// render()'s own command loop) - it's what a song uses in place of a
// short scene (each with its own length now, Scene::getLengthBars()) to
// end early, e.g. a short intro.
TEST(pattern_break_jumps_straight_to_the_destination_row_of_the_next_pattern) {
  Song song;
  song.setRowsPerBar(1); // Scene::length_bars_ defaults to 4 - together, a 4-row scene, matching this test's old patternRows=4
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  auto & scene0 = song.addScene();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));
  scene0.setCommand(0, track.getInternalId(), Command("ZB02"));
  // Rows 1-3 are deliberately non-empty too, to prove the break really
  // skips them rather than happening to land past them by coincidence.
  scene0.setNote(1, track.getInternalId(), 0, Note(61, 100));

  auto & scene1 = song.addScene();
  scene1.setNote(0, track.getInternalId(), 0, Note(62, 100)); // must never be reached
  scene1.setNote(2, track.getInternalId(), 0, Note(64, 100)); // the break's destination row

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  // Exactly one row's worth of samples: row 0 (with the ZB02 break) plays,
  // then the transport advances - straight to pattern 1's row 2 (absolute
  // row 1*4 + 2 = 6), not pattern 0's row 1 (absolute 1) or pattern 1's
  // row 0 (absolute 4).
  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(state.getAbsolutePosition() == 6);
}

// SongState::renderBlock()'s command scheduling loop reads Command from
// whatever track_id a scene's Pattern is keyed by, with no dependency on
// that track's type or position in the tree - a ZBxx entered on the
// master's own effect column (or any per-track Effect's) works exactly
// the same as one entered on an instrument track's.
TEST(pattern_break_on_the_master_tracks_own_column_works_too) {
  Song song;
  song.setRowsPerBar(1); // Scene::length_bars_ defaults to 4 - together, a 4-row scene, matching this test's old patternRows=4
  auto master_id = song.getMasterTrack().getInternalId();

  auto & scene0 = song.addScene();
  scene0.setCommand(0, master_id, Command("ZB02"));

  song.addScene();

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(state.getAbsolutePosition() == 6);
}

// Same as the master-track case above, but for a per-track Effect's own
// trailing column - wrapped_instrument_track's own Command is never even
// read (the effect's own is), it's only there to give the effect
// something to wrap.
TEST(pattern_break_on_a_wrapping_effects_own_column_works_too) {
  Song song;
  song.setRowsPerBar(1); // Scene::length_bars_ defaults to 4 - together, a 4-row scene, matching this test's old patternRows=4
  auto & effect = song.addTrack(make_unique<Amplifier>());
  effect.addChild(make_unique<InstrumentTrack>(0));

  auto & scene0 = song.addScene();
  scene0.setCommand(0, effect.getInternalId(), Command("ZB02"));

  song.addScene();

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(state.getAbsolutePosition() == 6);
}

// A break past the last pattern behaves exactly like normal end-of-song
// run-off (SongState::renderBlock() has no special-casing for it) - this just
// documents that it doesn't crash or wrap back to pattern 0.
TEST(pattern_break_past_the_last_pattern_does_not_crash) {
  Song song;
  song.setRowsPerBar(1); // Scene::length_bars_ defaults to 4 - together, a 4-row scene, matching this test's old patternRows=4
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  auto & scene0 = song.addScene();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));
  scene0.setCommand(0, track.getInternalId(), Command("ZB00"));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(state.getAbsolutePosition() == 4); // one pattern past the (only) one that exists
}
