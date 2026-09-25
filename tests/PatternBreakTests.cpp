#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/effects/Amplifier.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/audio/OfflineRenderer.h"

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

// ZBxx: once its row ends, the transport goes to locator xx (1-based)
// instead of the next row.
TEST(pattern_break_jumps_to_the_numbered_locator) {
  Song song;
  song.setRowsPerBar(1);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  auto & scene0 = song.getArrangement();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));
  scene0.setCommand(0, track.getInternalId(), Command("ZB02"));
  song.setLocator(1, "a");
  song.setLocator(6, "b");
  song.setLocator(3, "c");

  CHECK(positionAfterFirstRow(song) == 3); // the second in row order
}

TEST(pattern_break_00_jumps_to_the_next_locator) {
  Song song;
  song.setRowsPerBar(1);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZB00"));
  song.setLocator(0, "start");
  song.setLocator(6, "later");

  CHECK(positionAfterFirstRow(song) == 6);
}

TEST(pattern_break_00_wraps_from_the_last_locator_to_the_first) {
  Song song;
  song.setRowsPerBar(1);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZB00"));
  song.setLocator(0, "start");

  CHECK(positionAfterFirstRow(song) == 0);
}

// With no such locator, the break does nothing - the row just ends.
TEST(pattern_break_to_a_missing_locator_plays_on) {
  Song song;
  song.setRowsPerBar(1);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZB03"));
  song.setLocator(2, "only");

  CHECK(positionAfterFirstRow(song) == 1);
}

// The command loop reads Command from whatever track an arrangement Pattern
// is keyed by, whatever its type or position in the tree - the master's
// own column works the same as an instrument track's.
TEST(pattern_break_on_the_master_tracks_own_column_works_too) {
  Song song;
  song.setRowsPerBar(1);
  song.getArrangement().setCommand(0, song.getMasterTrack().getInternalId(), Command("ZB01"));
  song.setLocator(6, "b");

  CHECK(positionAfterFirstRow(song) == 6);
}

// Same for a per-track Effect's own column; the wrapped track's Command is
// never read.
TEST(pattern_break_on_a_wrapping_effects_own_column_works_too) {
  Song song;
  song.setRowsPerBar(1);
  auto & effect = song.addTrack(make_unique<Amplifier>());
  effect.addChild(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, effect.getInternalId(), Command("ZB01"));
  song.setLocator(6, "b");

  CHECK(positionAfterFirstRow(song) == 6);
}

// A break back to an earlier locator loops the song on the live transport,
// but an offline render plays one pass and ends.
TEST(offline_render_ends_where_a_pattern_break_jumps_back) {
  Song song;
  song.setRowsPerBar(1);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(3, track.getInternalId(), Command("ZB01"));
  song.setLocator(0, "top");

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(song, config);
  auto row_frames = static_cast<size_t>(config.getSampleInterval(song.getTempo()));
  CHECK(result.numberOfFrames() > 0);
  CHECK(result.numberOfFrames() < 16 * row_frames); // one pass plus a short tail, not an endless loop
}
