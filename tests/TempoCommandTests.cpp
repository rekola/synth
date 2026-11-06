#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/model/Clip.h"
#include "../src/model/ArrangementOps.h"

using namespace std;

namespace {
struct Rig {
  ChannelConfiguration config{44100, 1};
  unique_ptr<Mixer> mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state{config};
  const Song & song;

  explicit Rig(const Song & s) : song(s) {
    state.initialize(song);
    state.setIsPlaying(true);
  }
  // Plays whole rows at whatever tempo is in force.
  void play(int rows) {
    for (int r = 0; r < rows; r++) state.renderBlock(state.samplesUntilNextRow(), song, *mixer);
  }
};
} // namespace

TEST(tempo_command_parses_hex_bpm) {
  CHECK(Command("ZT78").isTempoSet());
  CHECK(Command("ZT78").getTempoBpm() == 120);
  CHECK(Command("ZTFF").getTempoBpm() == 255);
  CHECK(Command("ZT20").getTempoBpm() == 32);
  CHECK(Command("ZT1F").getTempoBpm() == 0); // below the range: no tempo
  CHECK(!Command("ZB78").isTempoSet());
}

// ZTxx sets the tempo from its own row on: that row is already the new length.
TEST(tempo_command_sets_the_tempo_from_its_row) {
  Song song;
  song.setTempo(90);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(2, track.getInternalId(), Command("ZT78"));

  Rig rig(song);
  rig.play(2);
  CHECK(rig.state.getTempo() == 90);
  rig.state.renderBlock(1, song, *rig.mixer);
  CHECK(rig.state.getTempo() == 120);
  CHECK(rig.state.samplesUntilNextRow() == rig.config.getSampleInterval(120) - 1);
  CHECK(song.getTempo() == 90); // the song's own tempo is where playing starts
}

TEST(tempo_command_below_range_does_nothing) {
  Song song;
  song.setTempo(90);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZT10"));

  Rig rig(song);
  rig.play(1);
  CHECK(rig.state.getTempo() == 90);
}

TEST(tempo_command_in_a_placed_clip_works_too) {
  Song song;
  song.setTempo(90);
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  Clip clip(track_id);
  clip.getLeafPattern().setNote(0, 0, Note(60, 100));
  clip.setLength(16);
  clip.getLeafPattern().setCommand(1, Command("ZT64"));
  song.addClip(move(clip));
  placeClipInstance(song, track_id, 0, 0);

  Rig rig(song);
  rig.play(2);
  CHECK(rig.state.getTempo() == 100);
}

// Of two tracks setting a tempo on the same row, the later one in the song wins.
TEST(tempo_command_of_the_later_track_wins) {
  Song song;
  auto first = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto second = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  song.getArrangement().setCommand(0, second, Command("ZT50"));
  song.getArrangement().setCommand(0, first, Command("ZT60"));

  Rig rig(song);
  rig.play(1);
  CHECK(rig.state.getTempo() == 0x50);
}

// A seek recomputes the tempo from the arrangement: the last ZTxx before the
// row, else the song's own.
TEST(tempo_after_a_seek_is_what_playing_there_gives) {
  Song song;
  song.setTempo(90);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(4, track.getInternalId(), Command("ZT78"));
  song.getArrangement().setCommand(8, track.getInternalId(), Command("ZTA0"));

  Rig rig(song);
  rig.state.setPosition(6);
  rig.state.renderBlock(1, song, *rig.mixer);
  CHECK(rig.state.getTempo() == 120);
  rig.state.setPosition(12);
  rig.state.renderBlock(1, song, *rig.mixer);
  CHECK(rig.state.getTempo() == 160);
  rig.state.setPosition(0);
  rig.state.renderBlock(1, song, *rig.mixer);
  CHECK(rig.state.getTempo() == 90);
}

// An edit of the song's tempo while a ZTxx is in force still applies at once.
TEST(tempo_edit_overrides_a_tempo_command) {
  Song song;
  song.setTempo(90);
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  song.getArrangement().setCommand(0, track.getInternalId(), Command("ZT78"));

  Rig rig(song);
  rig.play(1);
  CHECK(rig.state.getTempo() == 120);
  song.setTempo(100);
  rig.play(1);
  CHECK(rig.state.getTempo() == 100);
}
