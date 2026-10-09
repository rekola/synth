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
