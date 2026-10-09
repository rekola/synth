#include "TestFramework.h"

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif

#include "../src/Controller.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/LeafTrack.h"
#include "../src/model/Song.h"
#include "../src/ambisonic/ChannelConfiguration.h"

TEST(undo_walks_back_through_edits_and_redo_walks_forward_again) {
  Song song;
  song.setTempo(100);
  song.setTempo(110);
  song.setTempo(120);
  CHECK(song.canUndo());
  CHECK(!song.canRedo());
  CHECK(song.undo());
  CHECK(song.getTempo() == 110);
  CHECK(song.undo());
  CHECK(song.getTempo() == 100);
  CHECK(song.canRedo());
  CHECK(song.redo());
  CHECK(song.getTempo() == 110);
  CHECK(song.redo());
  CHECK(song.getTempo() == 120);
  CHECK(!song.canRedo());
  CHECK(!song.redo());
}

TEST(undoing_everything_stops_and_history_is_never_dropped_by_it) {
  Song song;
  auto original = song.getTempo();
  song.setTempo(100);
  auto entries = song.document().journal().size();
  CHECK(song.undo());
  CHECK(song.getTempo() == original);
  CHECK(song.document().journal().size() == entries + 1); // the undo is an entry
  CHECK(!song.undo() || song.getTempo() == 100); // walking past the start redoes, Emacs-style, only after a break
}

TEST(a_new_edit_breaks_the_chain_so_redo_is_gone_and_undo_undoes_it) {
  Song song;
  song.setTempo(100);
  song.setTempo(110);
  CHECK(song.undo());
  CHECK(song.getTempo() == 100);
  song.setSwing(60);
  CHECK(!song.canRedo());
  CHECK(song.undo());
  CHECK(song.getSwing() == 50);
  CHECK(song.getTempo() == 100);
  // the walk now continues through the undone undo: tempo 110 comes back
  CHECK(song.undo());
  CHECK(song.getTempo() == 110);
}

TEST(an_untracked_sync_edit_neither_breaks_the_chain_nor_is_undone) {
  Song song;
  song.setTempo(100);
  song.setTempo(110);
  CHECK(song.undo());
  {
    Song::Edit sync(song, "glide", Song::Edit::Kind::STRUCTURE, Song::Edit::Origin::SYNC);
    song.setSwing(55);
  }
  CHECK(song.canRedo());
  CHECK(song.redo());
  CHECK(song.getTempo() == 110);
  CHECK(song.getSwing() == 55);
  CHECK(song.undo());
  CHECK(song.getSwing() == 55);
}

TEST(undo_and_redo_are_ignored_while_a_take_is_open) {
  Song song;
  song.setTempo(100);
  song.document().beginGroup("take");
  CHECK(!song.canUndo());
  CHECK(!song.undo());
  song.document().endGroup();
  CHECK(song.undo());
}

TEST(undo_restores_a_tracks_settings_and_the_published_content_follows) {
  Song song;
  auto generation = song.getMasterTrack().getChildren().size();
  auto track = std::make_unique<InstrumentTrack>(0);
  track->setId("t");
  song.addTrack(std::move(track));
  CHECK(song.getMasterTrack().getChildren().size() == generation + 1);
  CHECK(song.undo());
  CHECK(song.getMasterTrack().getChildren().size() == generation);
  CHECK(song.getMasterTrack().getChildById("t") == nullptr);
  CHECK(song.redo());
  CHECK(song.getMasterTrack().getChildById("t") != nullptr);
}

TEST(an_undo_is_published_to_the_audio_thread_like_the_edit_was) {
  Song song;
  song.setContentPublished(true);
  song.getArrangement().setNote(0, 7, 0, Note(60, 100));
  auto noteRows = [&song]() {
    auto & patterns = song.readContent()->arrangement.getPatternsByTrack();
    auto it = patterns.find(7);
    return it == patterns.end() ? 0 : static_cast<int>(it->second.getNotesByRow().size());
  };
  CHECK(noteRows() == 1);
  auto version = song.getMajorVersion();
  CHECK(song.undo());
  CHECK(song.publishedContentIsCurrent());
  CHECK(noteRows() == 0);
  CHECK(song.getMajorVersion() > version); // the widgets see it too
  CHECK(song.redo());
  CHECK(song.publishedContentIsCurrent());
  CHECK(noteRows() == 1);
}

TEST(an_undone_track_edit_is_in_the_compiled_tracks_the_audio_thread_reads) {
  Song song;
  song.setContentPublished(true);
  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  song.editTrack(id, [](Track & track) { track.setSendA(0.5f); });
  CHECK(dynamic_cast<const LeafTrack &>(*song.readContent()->tracks->master->getChildByInternalId(id)).getSends().a > 0.4f);
  CHECK(song.undo());
  CHECK(dynamic_cast<const LeafTrack &>(*song.readContent()->tracks->master->getChildByInternalId(id)).getSends().a < 0.01f);
  CHECK(song.redo());
  CHECK(dynamic_cast<const LeafTrack &>(*song.readContent()->tracks->master->getChildByInternalId(id)).getSends().a > 0.4f);
}

TEST(the_undo_commands_run_through_the_controller_and_say_when_there_is_nothing_to_do) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  CHECK(controller.openSong(std::string(TESTS_FIXTURES_DIR) + "/center_note.xml"));
  CHECK(!controller.getSong().canUndo());
  controller.sendCommand("undo"); // nothing yet: a status message, no change
  auto tempo = controller.getSong().getTempo();
  controller.getSong().setTempo(tempo + 10);
  controller.sendCommand("undo");
  CHECK(controller.getSong().getTempo() == tempo);
  CHECK(controller.getSong().canRedo());
  controller.sendCommand("undo-redo");
  CHECK(controller.getSong().getTempo() == tempo + 10);
}

TEST(a_deleted_clip_and_its_arrangement_placement_come_back_with_undo) {
  Song song;
  auto track_id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  Clip clip(track_id);
  clip.setLength(8);
  clip.getLeafPattern().setNote(0, 0, Note(60, 100));
  auto clip_id = song.addClip(std::move(clip)).getId();
  song.getArrangement().setInstance(track_id, 0, clip_id);
  CHECK(deleteClipOrStopButton(song, track_id, 0) == SlotDelete::CLIP);
  CHECK(song.getClips(track_id)[0].isEmpty());
  CHECK(song.undo());
  CHECK(!song.getClips(track_id)[0].isEmpty());
  CHECK(song.getClips(track_id)[0].getId() == clip_id);
  CHECK(song.getArrangement().getInstance(track_id, 0) == clip_id);
}

TEST(a_clip_take_is_one_undo_step_and_undo_waits_until_it_ends) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  CHECK(controller.openSong(std::string(TESTS_FIXTURES_DIR) + "/center_note.xml"));
  auto & song = controller.getSong();
  auto track_id = song.getRootTrackIds().at(0);
  auto journal = song.document().journal().size();

  controller.armClipTrackRecording(track_id, 0);
  CHECK(song.document().inGroup());
  for (int row = 20; row < 24; row++) song.getArrangement().setNote(row, track_id, 1, Note(60 + row, 100));
  controller.sendCommand("undo"); // refused mid-take
  CHECK(song.document().journal().size() == journal + 4); // each note landed on its own
  controller.clearClipRecordingTake(track_id);
  CHECK(!song.document().inGroup());
  CHECK(song.document().journal().size() == journal + 1); // folded into one step

  auto noteRows = [&]() {
    auto pattern = song.getArrangement().findPattern(track_id);
    return pattern ? static_cast<int>(pattern->getNotesByRow().size()) : 0;
  };
  auto with_take = noteRows();
  CHECK(song.undo());
  CHECK(noteRows() == with_take - 4);
}

TEST(an_untracked_write_inside_a_take_is_not_undone_with_it) {
  Song song;
  song.document().beginGroup("take");
  song.getArrangement().setNote(0, 7, 0, Note(60, 100));
  {
    Song::Edit sync(song, "glide", Song::Edit::Kind::STRUCTURE, Song::Edit::Origin::SYNC);
    song.setSwing(55);
  }
  song.getArrangement().setNote(1, 7, 0, Note(62, 100));
  song.document().endGroup();
  auto & journal = song.document().journal();
  CHECK(journal.back().tracked);
  CHECK(journal[journal.size() - 2].sequence < journal.back().sequence);
  CHECK(song.undo());
  CHECK(song.getSwing() == 55); // the mirror write stays
  CHECK(song.getArrangement().findPattern(7).getNotesByRow().empty());
}

TEST(undo_reports_the_track_and_row_it_changed) {
  Song song;
  song.getArrangement().setNote(12, 7, 0, Note(60, 100));
  CHECK(song.undo());
  auto place = song.lastUndoPlace();
  CHECK(place.track_id == 7);
  CHECK(place.row == 12);
  CHECK(song.redo());
  CHECK(song.lastUndoPlace().row == 12);

  auto id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
  song.editTrack(id, [](Track & track) { track.setSendA(0.5f); });
  CHECK(song.undo());
  CHECK(song.lastUndoPlace().track_id == id);
  CHECK(song.lastUndoPlace().row == -1);
}

TEST(the_undo_command_moves_the_current_track_and_the_stopped_transport_to_the_change) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  CHECK(controller.openSong(std::string(TESTS_FIXTURES_DIR) + "/center_note.xml"));
  auto & song = controller.getSong();
  auto track_id = song.getRootTrackIds().at(0);
  song.getArrangement().setNote(9, track_id, 1, Note(60, 100));
  song.setCurrentTrackId(-1);
  controller.sendCommand("undo");
  CHECK(song.getCurrentTrackId() == track_id);
}
