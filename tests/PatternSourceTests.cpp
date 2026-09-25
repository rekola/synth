#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/ui/ArrangementPatternSource.h"
#include "../src/ui/ScenePatternSource.h"
#include "../src/model/PatternBlockOps.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/PatternGrid.h"
#include "../src/model/Section.h"
#include "../src/model/Song.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <memory>

using namespace std;

namespace {

// A fresh buffer (which starts with one section of its own), trimmed to two
// one-bar sections, plus an instrument track.
struct Fixture {
  ChannelConfiguration config{44100, 1};
  Controller controller{config};
  int track_id = -1;
  int rows = 0; // rows per section

  Fixture() {
    controller.switchToBuffer(controller.freshBufferName());
    auto & song = controller.getSong();
    track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    song.getSection(0).setLengthBars(1);
    song.addSection().setLengthBars(1);
    rows = song.getRowsPerBar();
  }
  Song & song() { return controller.getSong(); }
};

}

TEST(arrangement_source_cursor_moves_across_sections) {
  Fixture f;
  ArrangementPatternSource source(f.controller);

  CHECK(source.cursor() == (RowAddress{ 0, 0 }));
  source.moveCursor(f.rows + 2);
  CHECK(source.cursor() == (RowAddress{ 1, 2 }));
  source.moveCursor(-3);
  CHECK(source.cursor() == (RowAddress{ 0, f.rows - 1 }));
}

TEST(arrangement_source_normalize_carries_across_sections_and_past_the_end) {
  Fixture f;
  ArrangementPatternSource source(f.controller);

  CHECK(source.blockCount() == 2);
  CHECK(source.blockLength(0) == f.rows);
  CHECK(source.normalize(0, f.rows + 1) == (RowAddress{ 1, 1 }));
  CHECK(source.normalize(0, 2 * f.rows).block >= source.blockCount());
}

TEST(arrangement_source_notes_follow_a_placed_clip_but_commands_stay_on_the_background) {
  Fixture f;
  auto & song = f.song();
  Clip clip(f.track_id);
  clip.setLength(f.rows);
  clip.setLooping(true);
  clip.getLeafPattern().setNote(0, 0, Note(60, 100));
  song.addClip(move(clip));
  auto & section = song.getSection(0);
  placeClipInstance(song, section, f.track_id, 0, 0);
  section.setNote(0, f.track_id, 0, Note(40, 100));
  section.setCommand(0, f.track_id, Command("0L40"));
  ArrangementPatternSource source(f.controller);

  auto read = source.read(f.track_id, { 0, 0 });
  CHECK(read.is_instance);
  CHECK(read.pattern->getNote(read.effective_row, 0).getValue() == 60);
  CHECK(source.hasInstance(f.track_id, { 0, 0 }));
  CHECK(!source.hasInstance(f.track_id, { 1, 0 }));

  auto edit = source.edit(f.track_id, { 0, 1 });
  edit.pattern->setNote(edit.effective_row, 0, Note(62, 100));
  CHECK(song.getClips(f.track_id)[0].getLeafPattern().getNote(1, 0).getValue() == 62);

  // A block operation anchored on a clip row acts on the clip's notes,
  // but commands stay on the background.
  int row;
  auto grid = source.readGrid({ 0, 0 });
  auto notes = grid->find(f.track_id, 0, row);
  CHECK(notes != nullptr);
  CHECK(notes->getNote(row, 0).getValue() == 60);
  auto background = grid->findCommands(f.track_id, 0, row);
  CHECK(background != nullptr);
  CHECK(background->getNote(row, 0).getValue() == 40);
  CHECK(background->getCommand(row).isDefined());
}

TEST(arrangement_source_edit_grid_creates_a_section_only_when_asked) {
  Fixture f;
  ArrangementPatternSource source(f.controller);
  auto & song = f.song();

  source.editGrid({ 5, 0 }, false);
  CHECK(song.getSections().size() == 2);
  CHECK(source.annotations(5, false) != nullptr);
  CHECK(song.getSections().size() == 2);

  int row;
  auto grid = source.editGrid({ 3, 0 }, true);
  CHECK(song.getSections().size() == 4);
  grid->obtain(f.track_id, 1, row)->setNote(row, 0, Note(64, 100));
  CHECK(song.getSection(3).getNote(1, f.track_id, 0).getValue() == 64);
}

TEST(arrangement_source_stop_instance_only_acts_where_a_clip_is_placed) {
  Fixture f;
  auto & song = f.song();
  Clip clip(f.track_id);
  clip.setLength(f.rows);
  clip.setLooping(true);
  song.addClip(move(clip));
  placeClipInstance(song, song.getSection(0), f.track_id, 0, 0);
  ArrangementPatternSource source(f.controller);

  CHECK(!source.stopInstance(f.track_id, { 1, 0 }));
  CHECK(source.stopInstance(f.track_id, { 0, 4 }));
  CHECK(resolveInstanceAt(song, song.getSection(0), f.track_id, 4).clip_index == Section::kStopInstance);
  CHECK(resolveInstanceAt(song, song.getSection(0), f.track_id, 3).clip_index == 0);
}

// --- ScenePatternSource / SceneGrid ---

namespace {

// Track `track_id` gets `clip` at clip-list index `index`.
void placeClip(Song & song, int track_id, int index, int length, bool looping, int note_row = 0) {
  auto & clip = song.ensureClipAt(track_id, index);
  clip.setId(song.generateUniqueClipId());
  clip.setLength(length);
  clip.setLooping(looping);
  clip.getLeafPattern().setNote(note_row, 0, Note(60 + index, 100));
}

}

TEST(scene_source_scenes_are_the_used_clip_rows_plus_one_empty_one) {
  Fixture f;
  auto & song = f.song();
  ScenePatternSource source(f.controller);
  CHECK(source.blockCount() == 8); // never fewer than a Launchpad grid's rows
  CHECK(source.blockLength(0) == f.rows);

  placeClip(song, f.track_id, 2, 2 * f.rows, true);
  CHECK(source.blockCount() == 8);
  placeClip(song, f.track_id, 9, f.rows, true);
  CHECK(source.blockCount() == 11); // ten used, plus one empty to create in
  CHECK(source.blockLength(1) == f.rows); // an unused scene is one bar
  CHECK(source.blockLength(2) == 2 * f.rows); // as long as its longest clip
}

TEST(scene_source_reads_a_looping_clip_repeating_and_a_one_shot_ending) {
  Fixture f;
  auto & song = f.song();
  auto one_shot = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto longest = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeClip(song, f.track_id, 0, 4, true, 1);
  placeClip(song, one_shot, 0, 4, false, 1);
  placeClip(song, longest, 0, 8, true);
  ScenePatternSource source(f.controller);
  CHECK(source.blockLength(0) == 8);

  auto looping = source.read(f.track_id, { 0, 5 });
  CHECK(looping.pattern->getNote(looping.effective_row, 0).getValue() == 60);
  CHECK(looping.unwrapped_row == 5);
  CHECK(looping.repeat_length == 4); // row 5 is a repeat, dimmed

  auto ended = source.read(one_shot, { 0, 5 });
  CHECK(!ended.pattern->getNote(ended.effective_row, 0).isDefined());
  CHECK(!ended.is_instance);
  auto playing = source.read(one_shot, { 0, 1 });
  CHECK(playing.pattern->getNote(playing.effective_row, 0).getValue() == 60);
}

TEST(scene_source_editing_an_empty_slot_creates_a_looping_clip_of_the_scene_length) {
  Fixture f;
  auto & song = f.song();
  auto other = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeClip(song, other, 1, 2 * f.rows, true);
  ScenePatternSource source(f.controller);

  auto edit = source.edit(f.track_id, { 1, 3 });
  edit.pattern->setNote(edit.effective_row, 0, Note(64, 100));

  auto & clips = song.getClips(f.track_id);
  CHECK(clips.size() == 2);
  CHECK(clips[0].isEmpty()); // the hole before it stays a hole
  CHECK(!clips[1].getId().empty());
  CHECK(clips[1].getName() == "Clip 2");
  CHECK(clips[1].getLength() == 2 * f.rows);
  CHECK(clips[1].isLooping());
  CHECK(clips[1].getLeafPattern().getNote(3, 0).getValue() == 64);
}

TEST(scene_source_editing_past_a_one_shots_end_lengthens_it_to_the_bar) {
  Fixture f;
  auto & song = f.song();
  placeClip(song, f.track_id, 0, f.rows, false);
  auto other = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeClip(song, other, 0, 2 * f.rows, true);
  ScenePatternSource source(f.controller);

  auto edit = source.edit(f.track_id, { 0, f.rows + 1 });
  edit.pattern->setNote(edit.effective_row, 0, Note(65, 100));
  auto & clip = song.getClips(f.track_id)[0];
  CHECK(clip.getLength() == 2 * f.rows);
  CHECK(clip.getLeafPattern().getNote(f.rows + 1, 0).getValue() == 65);
}

TEST(scene_source_clearing_never_creates_clips) {
  Fixture f;
  auto & song = f.song();
  ScenePatternSource source(f.controller);
  auto grid = source.editGrid({ 0, 0 }, false);
  clearPatternBlock(*grid, 0, 3, { f.track_id }, 0, 0);
  CHECK(song.getClips(f.track_id).empty());
}

TEST(scene_source_cursor_moves_across_scenes_without_touching_the_transport) {
  Fixture f;
  auto & song = f.song();
  placeClip(song, f.track_id, 1, f.rows, true);
  ScenePatternSource source(f.controller);
  auto transport = f.controller.getPlaybackInfo().getRowIndex();

  source.moveCursor(f.rows + 2);
  CHECK(source.cursor() == (RowAddress{ 1, 2 }));
  source.moveCursor(-3);
  CHECK(source.cursor() == (RowAddress{ 0, f.rows - 1 }));
  source.moveCursor(100 * f.rows); // clamps at the last scene's last row
  CHECK(source.cursor() == (RowAddress{ 7, f.rows - 1 }));
  CHECK(f.controller.getPlaybackInfo().getRowIndex() == transport);
}

TEST(scene_source_shows_a_playing_tracks_playhead_at_the_cursor_row) {
  Fixture f;
  ScenePatternSource source(f.controller);
  source.setCursorTrack(f.track_id);
  source.setPlayheads({ { f.track_id, { 1, 5 } } });
  CHECK(source.cursor() == (RowAddress{ 1, 5 }));
  CHECK(source.playheadRow(f.track_id, 1) == 5);
  CHECK(!source.playheadRow(f.track_id, 0));
  CHECK(!source.hasAnnotations());
  CHECK(!source.showsClipIndirection());
  CHECK(!source.cursorFollowsTransport());
}

namespace {

// `track_id` gets a looping clip at `index` whose row r plays note base+r.
void placeCountingClip(Song & song, int track_id, int index, int length, int base) {
  auto & clip = song.ensureClipAt(track_id, index);
  clip.setId(song.generateUniqueClipId());
  clip.setLength(length);
  clip.setLooping(true);
  for (int row = 0; row < length; row++) clip.getLeafPattern().setNote(row, 0, Note(base + row, 100));
}

int noteAt(const ReadTarget & target) {
  auto & note = target.pattern->getNote(target.effective_row, 0);
  return note.isDefined() ? note.getValue() : -1;
}

}

TEST(scene_source_each_track_is_shown_at_its_own_position) {
  Fixture f;
  auto & song = f.song();
  auto other = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeCountingClip(song, f.track_id, 0, f.rows, 40);
  placeCountingClip(song, other, 2, f.rows, 80);
  ScenePatternSource source(f.controller);

  source.setCursorTrack(other);
  source.setCursor({ 2, 1 });
  source.setCursorTrack(f.track_id);
  source.moveCursor(3);
  CHECK(source.cursor() == (RowAddress{ 0, 3 }));
  CHECK(source.trackBlock(f.track_id) == 0);
  CHECK(source.trackBlock(other) == 2);

  // The other track, stopped, moved along with the cursor: the cursor row
  // shows it at its own position, rows around it follow on from there.
  CHECK(noteAt(source.read(f.track_id, { 0, 3 })) == 43);
  CHECK(noteAt(source.read(other, { 0, 3 })) == 84);
  CHECK(noteAt(source.read(other, { 0, 5 })) == 86);
  CHECK(noteAt(source.read(other, { 0, -2 })) == -1); // the scene before is empty

  // On the other track, the cursor is at that position.
  source.setCursorTrack(other);
  CHECK(source.cursor() == (RowAddress{ 2, 4 }));
}

TEST(scene_source_moving_the_cursor_moves_stopped_tracks_but_not_playing_ones) {
  Fixture f;
  auto & song = f.song();
  auto stopped = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto playing = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  ScenePatternSource source(f.controller);
  source.setCursorTrack(stopped);
  source.setCursor({ 1, 2 });
  source.setCursorTrack(f.track_id);
  source.setPlayheads({ { playing, { 2, 5 } } });

  source.moveCursor(f.rows + 1); // into the next scene
  CHECK(source.cursor() == (RowAddress{ 1, 1 }));
  CHECK(source.trackBlock(stopped) == 2);
  source.setCursorTrack(stopped);
  CHECK(source.cursor() == (RowAddress{ 2, 3 }));
  source.setCursorTrack(playing);
  CHECK(source.cursor() == (RowAddress{ 2, 5 })); // still on its playhead

  // The cursor stops at the first row; the stopped track moves only as far.
  source.setCursorTrack(f.track_id);
  source.moveCursor(-10 * f.rows);
  CHECK(source.cursor() == (RowAddress{ 0, 0 }));
  source.setCursorTrack(stopped);
  CHECK(source.cursor() == (RowAddress{ 1, 2 }));
}

TEST(scene_source_cursor_tracks_playhead_never_moves_stopped_tracks) {
  Fixture f;
  auto & song = f.song();
  auto stopped = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  ScenePatternSource source(f.controller);
  source.setCursorTrack(stopped);
  source.setCursor({ 0, 5 });
  source.setCursorTrack(f.track_id);

  source.setPlayheads({ { f.track_id, { 1, 0 } } });
  source.setPlayheads({ { f.track_id, { 1, 2 } } });
  source.setCursorTrack(stopped);
  CHECK(source.cursor() == (RowAddress{ 0, 5 }));
}

TEST(scene_source_playing_track_follows_its_playhead_and_stays_where_it_stops) {
  Fixture f;
  auto & song = f.song();
  auto other = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeCountingClip(song, other, 1, f.rows, 80);
  ScenePatternSource source(f.controller);
  source.setCursorTrack(f.track_id);
  source.setCursor({ 0, 2 });

  source.setPlayheads({ { other, { 1, 3 } } });
  CHECK(source.playheadRow(other, 0) == 2); // shown at the cursor row
  CHECK(noteAt(source.read(other, { 0, 2 })) == 83);

  source.setCursorTrack(other);
  CHECK(source.cursor() == (RowAddress{ 1, 3 }));
  CHECK(source.cursorLocked());
  source.moveCursor(2);
  source.setCursor({ 0, 0 });
  CHECK(source.cursor() == (RowAddress{ 1, 3 }));

  source.setPlayheads({ { other, { 1, 4 } } });
  source.setPlayheads({}); // it stops
  CHECK(!source.cursorLocked());
  CHECK(source.cursor() == (RowAddress{ 1, 4 }));
  source.moveCursor(1);
  CHECK(source.cursor() == (RowAddress{ 1, 5 }));
}

TEST(scene_source_playing_track_keeps_its_line_as_the_cursor_moves) {
  Fixture f;
  auto & song = f.song();
  auto playing = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeCountingClip(song, playing, 1, f.rows, 80);
  ScenePatternSource source(f.controller);
  source.setCursorTrack(f.track_id);
  source.setCursor({ 0, 2 });
  source.setPlayheads({ { playing, { 1, 3 } } });
  CHECK(source.playheadRow(playing, 0) == 2);
  CHECK(noteAt(source.read(playing, { 0, 2 })) == 83);

  // Moving the stopped cursor track leaves the playing column as it was.
  source.moveCursor(2);
  CHECK(source.cursor() == (RowAddress{ 0, 4 }));
  CHECK(source.playheadRow(playing, 0) == 2);
  CHECK(noteAt(source.read(playing, { 0, 2 })) == 83);
  CHECK(noteAt(source.read(playing, { 0, 4 })) == 85);

  // As it plays, its column scrolls under that line.
  source.setPlayheads({ { playing, { 1, 4 } } });
  CHECK(source.playheadRow(playing, 0) == 2);
  CHECK(noteAt(source.read(playing, { 0, 2 })) == 84);

  // Focused, the cursor row moves to its line - 2 rows up - taking the
  // stopped track along; back again, the cursor is where that left it.
  source.setCursorTrack(playing);
  CHECK(source.cursor() == (RowAddress{ 1, 4 }));
  CHECK(source.trackCursor(f.track_id) == source.cursor());
  source.setCursorTrack(f.track_id);
  CHECK(source.cursor() == (RowAddress{ 0, 2 }));
  CHECK(source.playheadRow(playing, 0) == 2);
  CHECK(noteAt(source.read(playing, { 0, 2 })) == 84);

  // Stopping, it stays as shown: its position is the row at the cursor row.
  source.setPlayheads({});
  CHECK(noteAt(source.read(playing, { 0, 2 })) == 84);
  source.setCursorTrack(playing);
  CHECK(source.cursor() == (RowAddress{ 1, 4 }));
}

TEST(scene_source_keeps_a_playing_tracks_line_in_view) {
  Fixture f;
  auto & song = f.song();
  auto playing = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  ScenePatternSource source(f.controller);
  source.setCursorTrack(f.track_id);
  source.setPlayheads({ { playing, { 1, 3 } } });
  source.moveCursor(10); // its line is left 10 rows above the cursor row
  RowAddress top = source.advance(source.cursor(), -3);
  CHECK(source.keepPlayheadsVisible(top, 8, 2));
  CHECK(source.rowsBetween(top, source.trackCursor(playing)) == 2);
  CHECK(!source.keepPlayheadsVisible(top, 8, 2));
}

TEST(scene_source_region_acts_on_each_track_at_its_own_position) {
  Fixture f;
  auto & song = f.song();
  auto other = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  placeCountingClip(song, f.track_id, 0, f.rows, 40);
  placeCountingClip(song, other, 1, f.rows, 80);
  ScenePatternSource source(f.controller);
  source.setCursorTrack(other);
  source.setCursor({ 1, 2 });
  source.setCursorTrack(f.track_id);

  // Rows 0-1 of the cursor track's scene: rows 2-3 of the other track's.
  auto grid = source.editGrid({ 0, 0 }, false);
  clearPatternBlock(*grid, 0, 1, { f.track_id, other }, 0, 1);
  auto & mine = song.getClips(f.track_id)[0].getLeafPattern();
  auto & theirs = song.getClips(other)[1].getLeafPattern();
  CHECK(!mine.getNote(0, 0).isDefined());
  CHECK(!mine.getNote(1, 0).isDefined());
  CHECK(mine.getNote(2, 0).getValue() == 42);
  CHECK(theirs.getNote(1, 0).getValue() == 81);
  CHECK(!theirs.getNote(2, 0).isDefined());
  CHECK(!theirs.getNote(3, 0).isDefined());
  CHECK(theirs.getNote(4, 0).getValue() == 84);
}

// --- SectionRegionGrid: block operations act on one content only ---

namespace {

// One track, a 16-row section with background notes on every row, and a
// 4-row one-shot clip (its own notes on every row) placed at row 4.
struct RegionFixture {
  Song song;
  int track_id;
  Section * section;
  RegionFixture() {
    track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    section = &song.addSection();
    section->setLengthBars(16 / std::max(1, song.getRowsPerBar()));
    Clip clip(track_id);
    clip.setLength(4);
    clip.setLooping(false);
    for (int row = 0; row < 4; row++) clip.getLeafPattern().setNote(row, 0, Note(70, 100));
    song.addClip(move(clip));
    placeClipInstance(song, *section, track_id, 4, 0);
    for (int row = 0; row < song.getEffectiveSectionLength(*section); row++) section->setNote(row, track_id, 0, Note(40, 100));
  }
  const Pattern & clipPattern() const { return song.getClips(track_id)[0].getLeafPattern(); }
};

}

TEST(region_grid_reports_where_each_content_supplies_a_track) {
  RegionFixture f;
  auto length = f.song.getEffectiveSectionLength(*f.section);
  CHECK((SectionRegionGrid(f.song, *f.section, 0, "").sourceRows(f.track_id) == make_pair(0, 3)));
  CHECK((SectionRegionGrid(f.song, *f.section, 5, "").sourceRows(f.track_id) == make_pair(4, 7)));
  CHECK((SectionRegionGrid(f.song, *f.section, 9, "").sourceRows(f.track_id) == make_pair(8, length - 1)));
}

TEST(region_grid_anchored_on_the_background_leaves_the_clip_alone) {
  RegionFixture f;
  SectionRegionGrid grid(f.song, *f.section, 0, "");
  clearPatternBlock(grid, 0, 7, { f.track_id }, 0, 0);
  CHECK(!f.section->getNote(2, f.track_id, 0).isDefined()); // background cleared
  CHECK(f.section->getNote(5, f.track_id, 0).isDefined()); // background under the clip: not shown, not touched
  CHECK(f.clipPattern().getNote(1, 0).isDefined()); // the clip untouched
}

TEST(region_grid_anchored_on_a_clip_acts_on_the_clip) {
  RegionFixture f;
  SectionRegionGrid grid(f.song, *f.section, 5, "");
  clearPatternBlock(grid, 0, 7, { f.track_id }, 0, 0);
  CHECK(!f.clipPattern().getNote(1, 0).isDefined()); // clip row 1 = section row 5
  CHECK(f.section->getNote(2, f.track_id, 0).isDefined()); // the background before it untouched
  CHECK(f.section->getNote(5, f.track_id, 0).isDefined()); // and the background under it
}

TEST(region_grid_paste_into_a_clip_stops_at_its_end) {
  RegionFixture f;
  PatternBlock block(4);
  for (auto & row : block) row.push_back({ { Note(64, 100) }, Command(), 0 });
  SectionRegionGrid grid(f.song, *f.section, 6, "");
  pastePatternBlock(grid, block, f.song.getEffectiveSectionLength(*f.section), 6, { f.track_id }, 0);
  CHECK(f.clipPattern().getNote(2, 0).getValue() == 64); // rows 6, 7 are the clip's rows 2, 3
  CHECK(f.clipPattern().getNote(3, 0).getValue() == 64);
  CHECK(f.section->getNote(8, f.track_id, 0).getValue() == 40); // past the clip: not written
}

TEST(region_grid_follows_a_focused_clip_across_the_whole_section) {
  RegionFixture f;
  auto focused = f.song.getClips(f.track_id)[0].getId();
  SectionRegionGrid grid(f.song, *f.section, 0, focused);
  CHECK((grid.sourceRows(f.track_id) == make_pair(0, f.song.getEffectiveSectionLength(*f.section) - 1)));
  int row;
  CHECK(grid.find(f.track_id, 1, row) == &f.clipPattern());
}
