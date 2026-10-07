#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/effects/Amplifier.h"
#include "../src/instruments/Oscillator.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/model/Clip.h"
#include "../src/model/ArrangementOps.h"

using namespace std;

namespace {
// Plays exactly one row of `song` from row 0 and returns where the
// transport went.
int positionAfterFirstRow(const Song & song) {
  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);
  state.renderBlock(config.getSampleInterval(song.getTempo()), song, *mixer);
  return state.getAbsolutePosition();
}
}

// ZBxx: once its row ends, the transport goes to row xx of the next bar
// instead of the next row.
TEST(pattern_break_jumps_to_the_next_bar) {
  Song song;
  song.setTimeSignature(TimeSignature{1, 4});
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZB00"));

  CHECK(positionAfterFirstRow(song) == 4);
}

TEST(pattern_break_starts_the_next_bar_at_the_given_row) {
  Song song;
  song.setTimeSignature(TimeSignature{2, 4});
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZB03"));

  CHECK(positionAfterFirstRow(song) == 11);
}

// A row past the end of the bar lands on its last row.
TEST(pattern_break_row_is_clamped_to_the_bar) {
  Song song;
  song.setTimeSignature(TimeSignature{2, 4});
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZBFF"));

  CHECK(positionAfterFirstRow(song) == 15);
}

// On a bar's last row the next bar is where the transport goes anyway.
TEST(pattern_break_on_a_bars_last_row_changes_nothing) {
  Song song;
  song.setTimeSignature(TimeSignature{1, 16});
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZB00"));

  CHECK(positionAfterFirstRow(song) == 1);
}

// The command loop reads Command from whatever track an arrangement Pattern
// is keyed by, whatever its type or position in the tree - the master's
// own column works the same as an instrument track's.
TEST(pattern_break_on_the_master_tracks_own_column_works_too) {
  Song song;
  song.setTimeSignature(TimeSignature{1, 4});
  song.getArrangement().setCommand(0, song.getMasterTrack().getInternalId(), Command("ZB00"));

  CHECK(positionAfterFirstRow(song) == 4);
}

// Same for a per-track Effect's own column; the wrapped track's Command is
// never read.
TEST(pattern_break_on_a_wrapping_effects_own_column_works_too) {
  Song song;
  song.setTimeSignature(TimeSignature{1, 4});
  auto & effect = song.addTrack(make_unique<Amplifier>());
  effect.addChild(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, effect.getInternalId(), Command("ZB00"));

  CHECK(positionAfterFirstRow(song) == 4);
}

// A placed clip's own break works too, so a scene can end early wherever
// it is played from.
TEST(pattern_break_inside_a_placed_clip_jumps_to_the_next_bar) {
  Song song;
  song.setTimeSignature(TimeSignature{1, 4});
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip clip(track_id);
  clip.getLeafPattern().setNote(0, 0, Note(60, 100));
  clip.getLeafPattern().setCommand(0, Command("ZB00"));
  song.addClip(move(clip));
  placeClipInstance(song, track_id, 0, 0);

  CHECK(positionAfterFirstRow(song) == 4);
}
