#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"

#include <filesystem>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

TEST(song_setters_write_the_document_and_leave_a_tracked_journal_entry) {
  Song song;
  auto & journal = song.document().journal();
  auto before = journal.size();
  song.setTempo(100);
  CHECK(song.getTempo() == 100);
  CHECK(journal.size() == before + 1);
  CHECK(journal.back().label == "set tempo");
  CHECK(journal.back().tracked);
}

TEST(song_edit_scope_makes_several_setters_one_journal_entry) {
  Song song;
  auto before = song.document().journal().size();
  {
    Song::Edit edit(song, "change meter");
    song.setTempo(90);
    song.setTimeSignature({3, 4});
    song.setSwing(60);
  }
  CHECK(song.document().journal().size() == before + 1);
  CHECK(song.document().journal().back().label == "change meter");
}

TEST(song_edits_undo_through_the_documents_inverse) {
  Song song;
  song.setTempo(100);
  {
    Song::Edit edit(song, "change");
    song.setTempo(150);
    song.setTimeSignature({6, 8});
    song.setLocator(8, "chorus");
  }
  CHECK(song.getTempo() == 150 && song.getTimeSignature() == TimeSignature({6, 8}) && song.getLocator(8) == "chorus");
  song.document().apply(doc::Document::inverse(song.document().journal().back()));
  CHECK(song.getTempo() == 100);
  CHECK(song.getTimeSignature() == TimeSignature({4, 4}));
  CHECK(song.getLocator(8).empty());
}

TEST(song_values_at_their_default_are_not_stored) {
  Song song;
  song.setTempo(100);
  song.setTempo(140); // back to the default
  CHECK(song.document().get(song.document().root())->find("tempo") == nullptr);
}

TEST(song_locators_stay_sorted_and_an_empty_name_removes_one) {
  Song song;
  song.setLocator(32, "c");
  song.setLocator(8, "a");
  song.setLocator(16, "b");
  song.setLocator(16, "b2");
  auto locators = song.getLocators();
  CHECK(locators.size() == 3);
  CHECK(song.getLocator(16) == "b2");
  CHECK(locators.begin()->first == 8 && locators.rbegin()->first == 32);
  song.setLocator(8, "");
  CHECK(song.getLocators().size() == 2);
  CHECK(song.getLocator(8).empty());
  CHECK(song.getArrangementLength() == 48); // 32 + 1, up to a whole bar
}

TEST(song_scenes_pad_up_to_the_scene_written_and_read_back) {
  Song song;
  CHECK(song.getSceneName(3).empty() && song.getSceneTempo(3) == 0 && !song.getSceneTimeSignature(3).isSet());
  song.setSceneFromText(2, "Waltz 3/4 90 BPM");
  CHECK(song.getSceneName(2) == "Waltz");
  CHECK(song.getSceneTempo(2) == 90);
  CHECK(song.getSceneTimeSignature(2) == TimeSignature({3, 4}));
  CHECK(song.getSceneName(0).empty());
  song.setSceneFromText(2, "0 BPM");
  CHECK(song.getSceneTempo(2) == 0);
}

TEST(running_bars_mirror_is_journaled_but_never_undone_on_its_own) {
  Song song;
  song.setRunningBars({ {3, 4}, 16 });
  CHECK(song.getRunningBars().isActive() && song.getRunningBars().origin == 16);
  CHECK(!song.document().journal().back().tracked);
  song.clearRunningBars();
  CHECK(!song.getRunningBars().isActive());
}

TEST(opening_a_song_leaves_no_undo_history_and_a_fresh_song_has_none) {
  Song fresh;
  CHECK(fresh.document().journal().empty());

  namespace fs = std::filesystem;
  auto path = (fs::path(TESTS_SCRATCH_DIR) / "song_document_roundtrip.xml").string();
  {
    Song song;
    song.setTempo(77);
    song.setSwing(60);
    song.setTimeSignature({5, 8});
    song.setLocator(4, "verse");
    song.setSceneFromText(1, "Fast 3/4 120 BPM");
    song.save(path);
  }
  Song loaded;
  InstrumentProvider provider;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getTempo() == 77 && loaded.getSwing() == 60);
  CHECK(loaded.getTimeSignature() == TimeSignature({5, 8}));
  CHECK(loaded.getLocator(4) == "verse");
  CHECK(loaded.getSceneName(1) == "Fast" && loaded.getSceneTempo(1) == 120);
  CHECK(loaded.document().journal().empty());
  fs::remove(path);
}

TEST(published_scalars_follow_the_setters) {
  Song song;
  song.setContentPublished(true);
  song.setTempo(111);
  CHECK(song.readContent()->scalars.tempo == 111);
  {
    Song::Edit edit(song, "meter");
    song.setTimeSignature({7, 8});
    CHECK(song.readContent()->scalars.time_signature == TimeSignature({4, 4})); // not until the edit closes
  }
  CHECK(song.readContent()->scalars.time_signature == TimeSignature({7, 8}));
  CHECK(song.publishedContentIsCurrent());
}

TEST(song_history_is_bounded) {
  Song song;
  for (size_t i = 0; i < Song::kJournalLimit + Song::kJournalSlack + 10; i++) song.setTempo(static_cast<short>(100 + i % 50));
  CHECK(song.document().journal().size() <= Song::kJournalLimit + Song::kJournalSlack);
  CHECK(song.document().journal().size() >= Song::kJournalLimit);
}

TEST(a_scene_launch_mirror_is_an_untracked_edit) {
  Song song;
  {
    Song::Edit edit(song, "scene tempo", Song::Edit::Kind::STRUCTURE, Song::Edit::Origin::SYNC);
    song.setTempo(150);
  }
  CHECK(song.getTempo() == 150);
  CHECK(!song.document().journal().back().tracked);
}
