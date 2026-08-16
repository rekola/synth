#include "TestFramework.h"

#include "../src/Song.h"
#include "../src/InstrumentTrack.h"
#include "../src/SongState.h"
#include "../src/MixerFactory.h"
#include "../src/MixerType.h"
#include "../src/Mixer.h"
#include "../src/ChannelConfiguration.h"

using namespace std;

// ZBxx is the first pattern effect command with real playback semantics
// (every other one in docs/commands.md is still a stub - see SongState::
// render()'s own command loop) - it's what a song now uses in place of a
// short pattern (every pattern in a song shares one length, Song::
// getPatternLength()) to end early, e.g. a short intro.
TEST(pattern_break_jumps_straight_to_the_destination_row_of_the_next_pattern) {
  Song song;
  song.setPatternLength(4);
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

// A break past the last pattern behaves exactly like normal end-of-song
// run-off (SongState::renderBlock() has no special-casing for it) - this just
// documents that it doesn't crash or wrap back to pattern 0.
TEST(pattern_break_past_the_last_pattern_does_not_crash) {
  Song song;
  song.setPatternLength(4);
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
