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

TEST(bar_grid_counts_bars_from_its_origin) {
  BarGrid bars; // 4/4 from row 0
  CHECK(bars.barStart(0) == 0 && bars.barStart(15) == 0 && bars.barStart(16) == 16);
  CHECK(bars.nextBarStart(0) == 16 && bars.nextBarStart(16) == 32);
  CHECK(bars.barIndex(33) == 2 && bars.barStartRow(2) == 32);
  CHECK(bars.roundUpToBar(32) == 32 && bars.roundUpToBar(33) == 48);
  CHECK(bars.barRows() == 16 && bars.beatRows() == 4);

  BarGrid waltz{ TimeSignature{ 3, 4 }, 20 }; // bars start at 20, 32, 44, ...
  CHECK(waltz.barStart(31) == 20 && waltz.barStart(32) == 32 && waltz.rowInBar(45) == 1);
  CHECK(waltz.barStart(19) == 8); // rows before the origin carry the same bars back
  CHECK(waltz.barIndex(20) == 0 && waltz.barIndex(19) == -1 && waltz.barIndex(44) == 2);
  CHECK(waltz.nextBarStart(44) == 56 && waltz.roundUpToBar(21) == 32);
  CHECK(waltz.barRows() == 12);
}

TEST(running_bars_override_the_songs_from_their_origin) {
  RunningBars none;
  CHECK(!none.isActive());
  CHECK(barsAt(TimeSignature{ 3, 4 }, none, 100).barRows() == 12);
  RunningBars running{ TimeSignature{ 5, 4 }, 64 };
  CHECK(barsAt(TimeSignature{ 3, 4 }, running, 63).barRows() == 12 && barsAt(TimeSignature{ 3, 4 }, running, 63).origin == 0);
  CHECK(barsAt(TimeSignature{ 3, 4 }, running, 64).barRows() == 20 && barsAt(TimeSignature{ 3, 4 }, running, 64).origin == 64);
}

TEST(the_songs_time_signature_drives_the_arrangement_bars) {
  Song song;
  CHECK(song.getTimeSignature() == (TimeSignature{ 4, 4 }) && song.getArrangementBars().barRows() == 16);
  song.setTimeSignature({ 3, 4 });
  CHECK(song.getArrangementBars().barRows() == 12 && song.isBarStart(12));
  song.setTimeSignature({ 5, 3 }); // not a note value a row divides: ignored
  CHECK(song.getTimeSignature() == (TimeSignature{ 3, 4 }));
}

TEST(position_counts_bars_in_the_bars_in_force) {
  Song song;
  CHECK(song.formatPosition(0) == "1.1.1");
  CHECK(song.formatPosition(21) == "2.2.2");
  song.setTimeSignature({ 3, 4 });
  CHECK(song.formatPosition(12) == "2.1.1" && song.formatPosition(23) == "2.3.4");
  song.setRunningBars({ { 6, 8 }, 24 }); // two bars of 3/4 in, 12 rows of 6/8 a bar from here
  CHECK(song.formatPosition(24) == "3.1.1"); // numbering carries on from the bar the origin sits in
  CHECK(song.formatPosition(26) == "3.2.1"); // a beat is two rows in 6/8
  CHECK(song.formatPosition(36) == "4.1.1");
}

TEST(the_time_signature_and_running_bars_round_trip_and_old_bar_lengths_load) {
  namespace fs = std::filesystem;
  InstrumentProvider provider;
  auto dir = fs::temp_directory_path() / "synth_timesig_roundtrip";
  fs::create_directories(dir);
  auto path = (dir / "song.xml").string();

  Song original;
  original.setTimeSignature({ 3, 4 });
  original.setRunningBars({ { 6, 8 }, 112 });
  original.save(path);
  Song loaded;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getTimeSignature() == (TimeSignature{ 3, 4 }));
  CHECK(loaded.getRunningBars().signature == (TimeSignature{ 6, 8 }) && loaded.getRunningBars().origin == 112);

  Song plain;
  plain.save(path);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(text.find("imeSignature") == std::string::npos && text.find("transport") == std::string::npos); // defaults aren't written
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(!reloaded.getRunningBars().isActive() && reloaded.getTimeSignature() == (TimeSignature{ 4, 4 }));

  // A song written with only a bar length keeps it as its signature.
  std::ofstream out(path);
  out << "<song tempo=\"100\" rowsPerBar=\"12\"><tracks/></song>";
  out.close();
  Song legacy;
  CHECK(legacy.open(path, provider));
  CHECK(legacy.getTimeSignature() == (TimeSignature{ 3, 4 }));
}
