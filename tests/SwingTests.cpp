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

TEST(scene_text_splits_into_name_and_tempo) {
  auto waltz = scenename::extract("Waltz 90 BPM");
  CHECK(waltz.has_tempo && waltz.tempo == 90 && waltz.name == "Waltz");
  auto tight = scenename::extract("90bpm waltz");
  CHECK(tight.tempo == 90 && tight.name == "waltz");
  auto middle = scenename::extract("Intro 140 Bpm slow");
  CHECK(middle.tempo == 140 && middle.name == "Intro slow");
  CHECK(!scenename::extract("Verse").has_tempo);
  CHECK(!scenename::extract("BPM").has_tempo);
  CHECK(!scenename::extract("5 bpm").has_tempo); // out of range, stays in the name
  auto clear = scenename::extract("Waltz 0 BPM");
  CHECK(clear.has_tempo && clear.tempo == 0 && clear.name == "Waltz");
  CHECK(scenename::extract("Waltz - bpm").has_tempo);
}

TEST(renaming_a_scene_keeps_its_tempo_until_cleared) {
  Song song;
  song.setSceneFromText(2, "Waltz 90 BPM");
  CHECK(song.getSceneName(2) == "Waltz" && song.getSceneTempo(2) == 90);
  song.setSceneFromText(2, "xyz");
  CHECK(song.getSceneName(2) == "xyz" && song.getSceneTempo(2) == 90);
  song.setSceneFromText(2, "xyz 0 BPM");
  CHECK(song.getSceneTempo(2) == 0);
  CHECK(song.getSceneTempo(-1) == 0);
}

TEST(scenes_round_trip_by_position) {
  namespace fs = std::filesystem;
  InstrumentProvider provider;
  auto dir = fs::temp_directory_path() / "synth_scene_roundtrip";
  fs::create_directories(dir);
  auto path = (dir / "scenes.xml").string();

  Song original;
  original.setSceneFromText(1, "Waltz 90 BPM");
  original.setSceneFromText(2, "Tempo only 120 BPM");
  original.setSceneName(2, "");
  original.setSceneFromText(4, "Tail");
  original.setSceneFromText(4, "");
  original.save(path);

  Song loaded;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getSceneName(0).empty() && loaded.getSceneTempo(0) == 0);
  CHECK(loaded.getSceneName(1) == "Waltz" && loaded.getSceneTempo(1) == 90);
  CHECK(loaded.getSceneName(2).empty() && loaded.getSceneTempo(2) == 120);
  CHECK(loaded.getSceneName(4).empty());
}

TEST(scene_text_carries_a_time_signature) {
  auto waltz = scenename::extract("Waltz 3/4 90 BPM");
  CHECK(waltz.has_time_signature && waltz.numerator == 3 && waltz.denominator == 4);
  CHECK(waltz.has_tempo && waltz.tempo == 90 && waltz.name == "Waltz");
  auto spaced = scenename::extract("Jig 6 / 8");
  CHECK(spaced.numerator == 6 && spaced.denominator == 8 && spaced.name == "Jig");
  CHECK(!scenename::extract("Take 2/3").has_time_signature); // 3 isn't a note value a row divides
  CHECK(!scenename::extract("Mix 100/4").has_time_signature);
  auto clear = scenename::extract("Waltz 0/4");
  CHECK(clear.has_time_signature && clear.numerator == 0 && clear.name == "Waltz");
}

TEST(scene_time_signature_sets_its_bar_and_beat_rows) {
  Song song;
  CHECK(song.getSceneBarRows(0) == 16 && song.getSceneBeatRows(0) == 4); // the song's own
  song.setSceneFromText(1, "Waltz 3/4");
  CHECK(song.getSceneBarRows(1) == 12 && song.getSceneBeatRows(1) == 4);
  song.setSceneFromText(2, "7/8");
  CHECK(song.getSceneBarRows(2) == 14 && song.getSceneBeatRows(2) == 2);
  song.setSceneFromText(1, "waltz");
  CHECK(song.getSceneBarRows(1) == 12); // renaming keeps it
  song.setSceneFromText(1, "0/4");
  CHECK(song.getSceneBarRows(1) == 16);
}

TEST(running_bars_count_from_their_origin) {
  Song song;
  CHECK(song.isBarStart(0) && song.isBarStart(16) && !song.isBarStart(12));
  song.setRunningBars({ { 3, 4 }, 32 }); // a waltz starting on the bar at row 32
  CHECK(song.isBarStart(32) && song.isBarStart(44) && !song.isBarStart(48));
  CHECK(song.rowInBar(47) == 3 && song.barStartAtOrBefore(47) == 44);
  CHECK(song.isBarStart(16) && !song.isBarStart(20)); // before the origin, the song's bars
  song.clearRunningBars();
  CHECK(song.isBarStart(48) && song.beatRowsAt(48) == 4);
}

TEST(scene_time_signature_round_trips) {
  namespace fs = std::filesystem;
  InstrumentProvider provider;
  auto dir = fs::temp_directory_path() / "synth_scene_timesig_roundtrip";
  fs::create_directories(dir);
  auto path = (dir / "scenes.xml").string();
  Song original;
  original.setSceneFromText(1, "Waltz 3/4 90 BPM");
  original.setSceneFromText(2, "5/8");
  original.save(path);
  Song loaded;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getSceneTimeSignature(1).numerator == 3 && loaded.getSceneTimeSignature(1).denominator == 4);
  CHECK(loaded.getSceneTempo(1) == 90 && loaded.getSceneName(1) == "Waltz");
  CHECK(loaded.getSceneTimeSignature(2).rowsPerBar() == 10);
  CHECK(!loaded.getSceneTimeSignature(0).isSet());
}

TEST(a_scene_without_a_signature_is_shown_in_the_running_bars) {
  Song song;
  song.setRunningBars({ { 3, 4 }, 0 });
  CHECK(song.getSceneBarRows(5) == 12 && song.getSceneBeatRows(5) == 4);
  song.setSceneFromText(5, "6/8");
  CHECK(song.getSceneBarRows(5) == 12 && song.getSceneBeatRows(5) == 2);
  song.setSceneFromText(5, "4/4");
  CHECK(song.getSceneBarRows(5) == 16);
}
