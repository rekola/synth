#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"

// A tempo edit while the transport plays takes effect within a block: at
// 8 kHz a row is 1000 frames at 120 bpm and 500 at 240.
TEST(a_live_tempo_change_shortens_the_rows_that_follow) {
  Song song;
  song.setTempo(120);
  ChannelConfiguration config(8000);
  CHECK(config.getSampleInterval(120) == 1000);
  CHECK(config.getSampleInterval(240) == 500);

  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(2000, song, *mixer);
  CHECK(state.getAbsolutePosition() == 2);

  song.setTempo(240);
  song.incVersion(); // what Controller::setTempo() does; the audio thread keys on it
  state.renderBlock(2000, song, *mixer);
  CHECK(state.getTempo() == 240);
  CHECK(state.getAbsolutePosition() == 6); // four more rows, not two
}

// The row in flight when the tempo rises may already be longer than the new
// row: it must end at once, not wait for a boundary that has already passed.
TEST(raising_the_tempo_mid_row_ends_an_already_overlong_row_immediately) {
  Song song;
  song.setTempo(120);
  ChannelConfiguration config(8000);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(1900, song, *mixer); // row 1, 900 frames in
  CHECK(state.getAbsolutePosition() == 1);
  song.setTempo(300); // a row is now 400 frames, shorter than the 900 already played
  song.incVersion();
  state.renderBlock(1000, song, *mixer);
  CHECK(state.getAbsolutePosition() > 2); // no stall on the missed boundary
  CHECK(state.getAbsolutePosition() <= 4);
}

TEST(tempo_commands_step_the_song_tempo_within_its_range_and_bump_the_version) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  controller.setTempo(120);
  CHECK(song.getTempo() == 120);

  auto version = song.getMajorVersion();
  CHECK(controller.sendCommand("tempo-increase"));
  CHECK(song.getTempo() == 121);
  CHECK(song.getMajorVersion() != version);
  CHECK(controller.sendCommand("tempo-decrease"));
  CHECK(controller.sendCommand("tempo-decrease"));
  CHECK(song.getTempo() == 119);

  controller.setTempo(1);
  CHECK(song.getTempo() == 20);
  controller.setTempo(1000);
  CHECK(song.getTempo() == 300);
}
