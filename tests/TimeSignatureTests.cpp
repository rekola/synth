#include "TestFramework.h"

#include "../src/model/BarGrid.h"
#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"

#include <filesystem>
#include <fstream>

TEST(time_signature_parses_and_gives_its_rows) {
  auto waltz = TimeSignature::parse("3/4");
  CHECK(waltz && waltz->rowsPerBar() == 12 && waltz->rowsPerBeat() == 4);
  auto spaced = TimeSignature::parse(" 6 / 8 ");
  CHECK(spaced && spaced->rowsPerBar() == 12 && spaced->rowsPerBeat() == 2);
  CHECK(!TimeSignature::parse("2/3"));
  CHECK(!TimeSignature::parse("100/4"));
  CHECK(!TimeSignature::parse("3"));
  CHECK(!TimeSignature::parse("33/4"));
  auto none = TimeSignature::parse("0/4");
  CHECK(none && !none->isSet());
  CHECK(TimeSignature::fromRowsPerBar(16) == (TimeSignature{ 4, 4 }));
  CHECK(TimeSignature::fromRowsPerBar(12) == (TimeSignature{ 3, 4 }));
  CHECK(TimeSignature::fromRowsPerBar(10).rowsPerBar() == 10);
}

TEST(bar_grid_is_four_four_until_a_marker) {
  BarGrid bars;
  CHECK(bars.barStart(0) == 0 && bars.barStart(15) == 0 && bars.barStart(16) == 16);
  CHECK(bars.nextBarStart(0) == 16 && bars.nextBarStart(16) == 32);
  CHECK(bars.barIndex(33) == 2 && bars.barStartRow(2) == 32);
  CHECK(bars.roundUpToBar(32) == 32 && bars.roundUpToBar(33) == 48);
  CHECK(bars.beatRows(5) == 4);
}

TEST(bar_grid_changes_bar_length_at_a_marker) {
  // 4/4 for two bars (rows 0-31), then 3/4, then 5/4 from row 56.
  BarGrid bars({ { 32, TimeSignature{ 3, 4 } }, { 56, TimeSignature{ 5, 4 } } });
  CHECK(bars.barStart(31) == 16 && bars.barStart(32) == 32);
  CHECK(bars.barStart(45) == 44 && bars.rowInBar(45) == 1);
  CHECK(bars.barRows(40) == 12 && bars.barRows(60) == 20 && bars.barRows(10) == 16);
  // 32..43, 44..55, then the 5/4 bar from 56.
  CHECK(bars.nextBarStart(44) == 56 && bars.nextBarStart(56) == 76);
  CHECK(bars.barIndex(31) == 1 && bars.barIndex(32) == 2 && bars.barIndex(44) == 3 && bars.barIndex(56) == 4 && bars.barIndex(76) == 5);
  CHECK(bars.barStartRow(2) == 32 && bars.barStartRow(3) == 44 && bars.barStartRow(4) == 56 && bars.barStartRow(5) == 76);
  for (int bar = 0; bar < 8; bar++) CHECK(bars.barIndex(bars.barStartRow(bar)) == bar);
}

TEST(a_marker_off_a_bar_start_cuts_the_bar_before_it_short) {
  BarGrid bars({ { 20, TimeSignature{ 3, 4 } } }); // bar 1 would run 16..31 in 4/4
  CHECK(bars.barStart(19) == 16 && bars.nextBarStart(16) == 20);
  CHECK(bars.barIndex(19) == 1 && bars.barIndex(20) == 2);
  CHECK(bars.barStartRow(2) == 20);
}

TEST(a_marker_at_row_zero_sets_the_first_signature) {
  BarGrid bars({ { 0, TimeSignature{ 3, 4 } } });
  CHECK(bars.barRows(0) == 12 && bars.barStart(13) == 12);
  CHECK(bars.barStart(-1) == -12); // rows before 0 carry the first bars back
}

TEST(song_markers_drive_the_arrangement_bars_and_clear) {
  Song song;
  CHECK(song.getArrangementTimeSignature(100) == (TimeSignature{ 4, 4 }));
  song.setTimeSignatureMarker(32, { 3, 4 });
  CHECK(song.getArrangementBars()->barRows(40) == 12);
  CHECK(song.getArrangementTimeSignature(31) == (TimeSignature{ 4, 4 }));
  CHECK(song.getArrangementTimeSignature(32) == (TimeSignature{ 3, 4 }));
  song.setTimeSignatureMarker(32, { 5, 3 }); // not a note value a row divides: ignored, removing it
  CHECK(song.getArrangementBars()->barRows(40) == 16);
  song.setTimeSignatureMarker(32, { 3, 4 });
  song.clearTimeSignatureMarker(32);
  CHECK(song.getTimeSignatureMarkers().empty());
}

TEST(the_transports_bars_follow_the_arrangements_until_a_scene_sets_its_own) {
  Song song;
  song.setTimeSignatureMarker(32, { 3, 4 });
  CHECK(song.isBarStart(44) && song.barRowsAt(40) == 12 && song.nextBarStart(33) == 44);
  song.setTransportBars({ 7, 8 }, 64);
  CHECK(song.barRowsAt(70) == 14 && song.beatRowsAt(70) == 2 && song.isBarStart(78));
  CHECK(song.barRowsAt(40) == 12); // before its origin, the arrangement's
  CHECK(song.getRunningTimeSignature() == (TimeSignature{ 7, 8 }));
  song.clearTransportBars();
  CHECK(song.barRowsAt(70) == 12);
}

TEST(position_counts_bars_in_the_signature_in_force) {
  Song song;
  CHECK(song.formatPosition(0) == "1.1.1");
  CHECK(song.formatPosition(21) == "2.2.2");
  song.setTimeSignatureMarker(32, { 3, 4 });
  CHECK(song.formatPosition(32) == "3.1.1");
  CHECK(song.formatPosition(44) == "4.1.1");
  CHECK(song.formatPosition(47) == "4.1.4");
  song.setTransportBars({ 6, 8 }, 56);
  CHECK(song.formatPosition(56) == "5.1.1"); // numbering carries on from the bar the origin sits in
  CHECK(song.formatPosition(58) == "5.2.1"); // a beat is two rows in 6/8
  CHECK(song.formatPosition(68) == "6.1.1");
}

TEST(the_arrangement_length_is_a_bar_start_of_its_bars) {
  Song song;
  song.setTimeSignatureMarker(0, { 3, 4 });
  CHECK(song.getArrangementLength() == 0);
}

TEST(markers_and_the_transports_bars_round_trip_and_old_bar_lengths_load) {
  namespace fs = std::filesystem;
  InstrumentProvider provider;
  auto dir = fs::temp_directory_path() / "synth_timesig_roundtrip";
  fs::create_directories(dir);
  auto path = (dir / "song.xml").string();

  Song original;
  original.setTimeSignatureMarker(32, { 3, 4 });
  original.setTimeSignatureMarker(56, { 5, 4 });
  original.setTransportBars({ 6, 8 }, 112);
  original.save(path);
  Song loaded;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getTimeSignatureMarkers().size() == 2);
  CHECK(loaded.getArrangementTimeSignature(40) == (TimeSignature{ 3, 4 }));
  CHECK(loaded.getArrangementTimeSignature(60) == (TimeSignature{ 5, 4 }));
  CHECK(loaded.getTransportBars()->signature == (TimeSignature{ 6, 8 }) && loaded.getTransportBars()->origin == 112);

  Song plain;
  plain.save(path);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(text.find("timeSignature") == std::string::npos && text.find("transport") == std::string::npos); // defaults aren't written
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(!reloaded.getTransportBars()->isActive() && reloaded.getTimeSignatureMarkers().empty());

  // A song written with only a bar length keeps it as its first signature.
  std::ofstream out(path);
  out << "<song tempo=\"100\" rowsPerBar=\"12\"><tracks/></song>";
  out.close();
  Song legacy;
  CHECK(legacy.open(path, provider));
  CHECK(legacy.getArrangementBars()->barRows(0) == 12);
}
