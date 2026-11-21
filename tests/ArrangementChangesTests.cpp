#include "TestFramework.h"

#include "../src/instruments/InstrumentProvider.h"
#include "../src/model/BarGrid.h"
#include "../src/model/Song.h"

#include <map>
#include <string>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

TEST(arrangement_bars_count_across_signature_changes) {
  // 4/4 for two bars (rows 0-31), then 3/4 for two (32-55), then 6/8.
  ArrangementBars bars({4, 4}, {{32, {3, 4}}, {56, {6, 8}}});
  CHECK(bars.barIndex(0) == 0 && bars.barIndex(31) == 1);
  CHECK(bars.barIndex(32) == 2 && bars.barIndex(44) == 3 && bars.barIndex(55) == 3);
  CHECK(bars.barIndex(56) == 4 && bars.barIndex(68) == 5);
  CHECK(bars.barStartRow(2) == 32 && bars.barStartRow(3) == 44 && bars.barStartRow(5) == 68);
  CHECK(bars.barRows(10) == 16 && bars.barRows(40) == 12 && bars.barRows(60) == 12);
  CHECK(bars.beatRows(40) == 4 && bars.beatRows(60) == 6);
  CHECK(bars.roundUpToBar(33) == 44 && bars.roundUpToBar(44) == 44);
  CHECK(bars.rowInBar(47) == 3);
}

TEST(arrangement_bars_without_changes_are_the_songs) {
  ArrangementBars bars({3, 4}, {});
  CHECK(!bars.hasChanges());
  CHECK(bars.barIndex(25) == 2 && bars.barStartRow(2) == 24 && bars.barRows(100) == 12);
}

TEST(a_change_off_a_bar_start_cuts_the_bar_before_it_short) {
  ArrangementBars bars({4, 4}, {{20, {3, 4}}});
  CHECK(bars.barIndex(19) == 1 && bars.barIndex(20) == 2);
  CHECK(bars.nextBarStart(17) == 20);
}

TEST(tempo_signature_changes_and_locators_round_trip_and_undo) {
  InstrumentProvider provider;
  Song song;
  song.setTempo(100);
  song.setTimeSignature({2, 4});
  {
    Song::Edit edit(song, "changes");
    song.setTempoChange(16, 80);
    song.setTempoChange(32, 90);
    song.setSignatureChange(24, {12, 8});
    song.setLocator(8, "Verse");
  }
  CHECK((song.getTempoChanges() == std::map<int, int>{{16, 80}, {32, 90}}));
  CHECK(song.getSignatureChanges().at(24) == (TimeSignature{12, 8}));
  CHECK(song.getArrangementTempoAt(15) == 100 && song.getArrangementTempoAt(16) == 80 && song.getArrangementTempoAt(40) == 90);
  CHECK(song.getArrangementBars().barRows(30) == 24);

  auto path = std::string(TESTS_SCRATCH_DIR) + "/arrangement_changes_round_trip.xml";
  song.save(path);
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getTempoChanges() == song.getTempoChanges());
  CHECK(reloaded.getSignatureChanges() == song.getSignatureChanges());
  CHECK(reloaded.getLocator(8) == "Verse");

  // Setting the same value again, or clearing nothing, writes nothing.
  auto version = song.getMajorVersion();
  song.setTempoChange(16, 80);
  song.setSignatureChange(99, {});
  CHECK(song.getMajorVersion() == version);

  song.setTempoChange(16, 0);
  song.setSignatureChange(24, {});
  CHECK((song.getTempoChanges() == std::map<int, int>{{32, 90}}));
  CHECK(song.getSignatureChanges().empty());
}

TEST(a_change_after_the_content_extends_the_arrangement) {
  Song song;
  song.setTimeSignature({2, 4});
  song.setTempoChange(20, 90);
  CHECK(song.getArrangementLength() == 24);
}

TEST(launch_quantization_counts_rows_from_the_bar_origin_or_defaults_to_the_bar) {
  BarGrid bars{{6, 8}, 0};
  CHECK(bars.isLaunchRow(12, 0) && !bars.isLaunchRow(24 - 1, 0));
  CHECK(bars.isLaunchRow(24, 24) && !bars.isLaunchRow(12, 24) && bars.isLaunchRow(48, 24));
  BarGrid running{{6, 8}, 5};
  CHECK(running.isLaunchRow(29, 24) && !running.isLaunchRow(24, 24) && running.isLaunchRow(5, 24));
}

TEST(launch_quantization_round_trips_and_is_clamped) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.getLaunchQuantization() == 0);
  song.setLaunchQuantization(24);
  CHECK(song.isLaunchRow(24) && !song.isLaunchRow(16) && song.isLaunchRow(48));
  auto path = std::string(TESTS_SCRATCH_DIR) + "/launch_quantization_round_trip.xml";
  song.save(path);
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getLaunchQuantization() == 24);
  song.setLaunchQuantization(100000);
  CHECK(song.getLaunchQuantization() == Song::kMaxLaunchQuantization);
  song.setLaunchQuantization(-3);
  CHECK(song.getLaunchQuantization() == 0);
}
