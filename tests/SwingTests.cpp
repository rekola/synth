#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/Swing.h"
#include "../src/instruments/InstrumentProvider.h"

#include <filesystem>
#include <fstream>

TEST(swing_offset_only_moves_the_second_eighth_of_each_pair) {
  CHECK(swing::offsetRows(0, 75) == 0.0f);
  CHECK(swing::offsetRows(1, 75) == 0.0f);
  CHECK(swing::offsetRows(3, 75) == 0.0f);
  CHECK_NEAR(swing::offsetRows(2, 75), 1.0f, 1e-6f);
  CHECK_NEAR(swing::offsetRows(6, 75), 1.0f, 1e-6f);
  CHECK_NEAR(swing::offsetRows(2, 67), 0.68f, 1e-5f);
}

TEST(straight_swing_offsets_nothing) {
  for (int row = 0; row < 16; row++) CHECK(swing::offsetRows(row, 50) == 0.0f);
}

TEST(swing_clamps_to_straight_and_maximum) {
  CHECK(swing::clamp(10) == 50);
  CHECK(swing::clamp(50) == 50);
  CHECK(swing::clamp(63) == 63);
  CHECK(swing::clamp(75) == 75);
  CHECK(swing::clamp(99) == 75);
  Song song;
  song.setSwing(120);
  CHECK(song.getSwing() == 75);
  song.setSwing(0);
  CHECK(song.getSwing() == 50);
}

TEST(swing_offset_ignores_negative_rows_without_moving_them_wrongly) {
  CHECK_NEAR(swing::offsetRows(-2, 75), 1.0f, 1e-6f); // -2 sits on a pair's second eighth too
  CHECK(swing::offsetRows(-1, 75) == 0.0f);
}

TEST(swing_round_trips_through_the_song_file_and_defaults_to_straight) {
  namespace fs = std::filesystem;
  InstrumentProvider provider;
  auto dir = fs::temp_directory_path() / "synth_swing_roundtrip";
  fs::create_directories(dir);
  auto path = (dir / "swing.xml").string();

  Song original;
  CHECK(original.getSwing() == 50);
  original.setSwing(67);
  original.save(path);

  Song loaded;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getSwing() == 67);

  Song straight;
  straight.save(path);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(text.find("swing=") == std::string::npos); // straight is the default, so it isn't written
  Song reloaded;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getSwing() == 50);

  fs::remove_all(dir);
}

#include "../src/Controller.h"

TEST(swing_commands_step_the_song_swing_within_its_range_and_bump_the_version) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  CHECK(song.getSwing() == 50);

  auto version = song.getMajorVersion();
  CHECK(controller.sendCommand("swing-increase"));
  CHECK(song.getSwing() == 51);
  CHECK(song.getMajorVersion() != version); // the audio thread keys on this

  CHECK(controller.sendCommand("swing-decrease"));
  CHECK(controller.sendCommand("swing-decrease"));
  CHECK(song.getSwing() == 50); // never below straight

  controller.setSwing(74);
  CHECK(controller.sendCommand("swing-increase"));
  CHECK(controller.sendCommand("swing-increase"));
  CHECK(song.getSwing() == 75); // nor above the maximum
}
