#include "TestFramework.h"

#include "../src/model/InstrumentTrack.h"
#include "../src/model/Song.h"

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
