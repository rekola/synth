#include "TestFramework.h"
#include "model/Song.h"

TEST(song_edit_bumps_the_version_once_when_the_outermost_scope_closes) {
  Song song;
  auto before = song.getMajorVersion();
  {
    Song::Edit outer(song, "outer");
    {
      Song::Edit inner(song, "inner");
      song.setTempo(100);
    }
    CHECK(song.getMajorVersion() == before); // nothing published mid-action
    song.setSwing(60);
  }
  CHECK(song.getMajorVersion() == before + 1);
}

TEST(song_edit_content_bumps_only_the_minor_counter) {
  Song song;
  auto major = song.getMajorVersion();
  auto minor = song.getMinorVersion();
  { Song::Edit edit(song, "note", Song::Edit::Kind::CONTENT); }
  CHECK(song.getMajorVersion() == major);
  CHECK(song.getMinorVersion() == minor + 1);
}

TEST(song_edit_structure_inside_content_wins) {
  Song song;
  auto major = song.getMajorVersion();
  auto minor = song.getMinorVersion();
  {
    Song::Edit outer(song, "note", Song::Edit::Kind::CONTENT);
    Song::Edit inner(song, "structure");
  }
  CHECK(song.getMajorVersion() == major + 1);
  CHECK(song.getMinorVersion() == minor);
}

TEST(song_edit_discard_leaves_the_version_alone) {
  Song song;
  auto version = song.getVersion();
  {
    Song::Edit edit(song, "nothing");
    edit.discard();
  }
  CHECK(song.getVersion() == version);
}

TEST(song_edit_discard_in_an_outer_scope_keeps_an_inner_write) {
  Song song;
  auto major = song.getMajorVersion();
  {
    Song::Edit outer(song, "outer");
    { Song::Edit inner(song, "wrote"); }
    outer.discard();
  }
  CHECK(song.getMajorVersion() == major + 1);
}
