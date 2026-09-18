#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/ui/ArrangementPatternSource.h"
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

  int row;
  auto grid = source.readGrid(0);
  auto background = grid->find(f.track_id, 0, row);
  CHECK(background != nullptr);
  CHECK(background->getNote(row, 0).getValue() == 40);
  CHECK(background->getCommand(row).isDefined());
}

TEST(arrangement_source_edit_grid_creates_a_section_only_when_asked) {
  Fixture f;
  ArrangementPatternSource source(f.controller);
  auto & song = f.song();

  source.editGrid(5, false);
  CHECK(song.getSections().size() == 2);
  CHECK(source.annotations(5, false) != nullptr);
  CHECK(song.getSections().size() == 2);

  int row;
  auto grid = source.editGrid(3, true);
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
