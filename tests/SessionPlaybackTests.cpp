#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/state/SongState.h"
#include "../src/state/SessionTrackInfo.h"
#include "../src/state/TrackInfo.h"
#include "../src/state/ActiveVoiceInfo.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace std;

// Session view launches inside the transport (SongState::
// queueSessionChange()): a launched clip takes its track over from the
// arrangement at the next bar, while every other track keeps following
// the arrangement.

namespace {

// Two tracks over 16 rows (4 bars of 4 rows). Each track's
// background plays a note on every row whose value names the row -
// `a` 40+row, `b` 70+row - and `a` has one 4-row clip playing 100+row.
struct SessionSong {
  Song song;
  int a = -1, b = -1;
  ChannelConfiguration config{44100, 1};
  unique_ptr<Mixer> mixer;
  unique_ptr<SongState> state;
  int seq = 0;

  explicit SessionSong(bool looping = true) {
    song.setTimeSignatureMarker(0, TimeSignature::fromRowsPerBar(4));
    song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
    a = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    b = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    auto & arrangement = song.getArrangement();
    for (int row = 0; row < 16; row++) {
      arrangement.setNote(row, a, 0, Note(40 + row, 100));
      arrangement.setNote(row, b, 0, Note(70 + row, 100));
    }
    Clip clip(a);
    clip.setLength(4);
    clip.setLooping(looping);
    for (int row = 0; row < 4; row++) clip.getLeafPattern().setNote(row, 0, Note(100 + row, 100));
    song.addClip(move(clip));

    mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    state = make_unique<SongState>(config);
    state->initialize(song);
    state->setIsPlaying(true);
  }

  void queue(int track_id, int target) { state->queueSessionChange(track_id, target, ++seq); }

  // Plays `rows` rows, one block each.
  void play(int rows) {
    for (int i = 0; i < rows; i++) state->renderBlock(config.getSampleInterval(song.getTempo()), song, *mixer);
  }

  // Whether `value` is sounding on the track - each row's note names
  // which row of which content played it.
  bool plays(int track_id, int value) const {
    unordered_map<int, vector<ActiveVoiceInfo> > voices;
    state->getAllActiveVoices(voices);
    auto it = voices.find(track_id);
    if (it == voices.end()) return false;
    return any_of(it->second.begin(), it->second.end(), [&](auto & voice) { return voice.note_value == value; });
  }
};

}

TEST(session_launch_takes_one_track_over_while_another_follows_the_arrangement) {
  SessionSong s;
  s.queue(s.a, 0);
  s.play(1); // row 0 is a bar start, so the launch takes effect at once
  CHECK(s.plays(s.a, 100));
  CHECK(!s.plays(s.a, 40));
  CHECK(s.plays(s.b, 70));
  s.play(5);
  CHECK(s.plays(s.a, 101)); // clip row 5 % 4
  CHECK(!s.plays(s.a, 45));
  CHECK(s.plays(s.b, 75));
  CHECK(s.state->getAbsolutePosition() == 6); // the arrangement's position is unaffected
  CHECK(s.state->getSessionTracks().at(s.a).clip_index == 0);
}

TEST(session_launch_waits_for_the_next_bar) {
  SessionSong s;
  s.play(1);
  s.queue(s.a, 0);
  s.play(3); // rows 1-3
  CHECK(s.plays(s.a, 43));
  CHECK(!s.plays(s.a, 100));
  s.play(1); // row 4, the bar
  CHECK(s.plays(s.a, 100));
  CHECK(!s.plays(s.a, 44));
}

TEST(session_back_to_arrangement_takes_effect_at_the_next_bar) {
  SessionSong s;
  s.queue(s.a, 0);
  s.play(6); // rows 0-5
  s.queue(s.a, SessionTrackInfo::kArrangement);
  s.play(2); // rows 6-7, still the clip
  CHECK(s.plays(s.a, 103));
  CHECK(!s.plays(s.a, 47));
  s.play(1); // row 8, the bar: the arrangement's own row 8
  CHECK(s.plays(s.a, 48));
  CHECK(s.state->getSessionTracks().count(s.a) == 0);
}

TEST(session_stopped_track_stays_silent_while_the_arrangement_plays) {
  SessionSong s;
  s.queue(s.a, SessionTrackInfo::kSilent);
  s.play(6);
  for (int row = 0; row < 6; row++) CHECK(!s.plays(s.a, 40 + row));
  CHECK(s.plays(s.b, 75));
  CHECK(s.state->getSessionTracks().at(s.a).clip_index == SessionTrackInfo::kSilent);
}

TEST(session_launched_one_shot_goes_silent_after_its_length) {
  SessionSong s(false);
  s.queue(s.a, 0);
  s.play(4);
  CHECK(s.plays(s.a, 103));
  s.play(3);
  CHECK(!s.plays(s.a, 100)); // no second lap
  for (int row = 4; row < 7; row++) CHECK(!s.plays(s.a, 40 + row));
  CHECK(s.state->getSessionTracks().at(s.a).clip_index == SessionTrackInfo::kSilent);
}

TEST(session_launched_clip_keeps_its_place_across_a_seek) {
  SessionSong s;
  s.queue(s.a, 0);
  s.play(2); // clip rows 0-1
  s.state->setPosition(9); // a mid-bar seek in the arrangement
  s.play(1);
  CHECK(s.plays(s.a, 102)); // the clip carries on from row 2
  CHECK(s.plays(s.b, 79));
}

TEST(session_transport_stop_silences_launched_clips) {
  SessionSong s;
  s.queue(s.a, 0);
  s.play(2);
  s.state->setIsPlaying(false);
  s.state->silenceSession(-1);
  CHECK(s.state->getSessionTracks().at(s.a).clip_index == SessionTrackInfo::kSilent);
  s.state->setIsPlaying(true);
  s.play(3); // rows 2-4
  CHECK(s.plays(s.b, 74));
  CHECK(!s.plays(s.a, 102));
  CHECK(!s.plays(s.a, 44));
}

TEST(session_launched_clips_own_volume_command_plays) {
  SessionSong s;
  s.song.getClips(s.a)[0].getLeafPattern().setCommand(1, Command("0L80"));
  s.queue(s.a, 0);
  s.play(2);
  unordered_map<int, TrackInfo> info;
  s.state->getAllTrackInfo(info);
  CHECK_NEAR(info[s.a].getLiveSendMain(), Command("0L80").getSendSetLinear(), 1e-4);
  CHECK_NEAR(info[s.b].getLiveSendMain(), 1.0f, 1e-4);
}

TEST(session_taken_over_track_ignores_its_arrangement_automation) {
  SessionSong s;
  s.song.getArrangement().setCommand(1, s.a, Command("0L10"));
  s.queue(s.a, 0);
  s.play(2);
  unordered_map<int, TrackInfo> info;
  s.state->getAllTrackInfo(info);
  CHECK_NEAR(info[s.a].getLiveSendMain(), 1.0f, 1e-4);
}

// -Rxy re-fires the notes still playing on its track - never a note a
// clip launched before it left behind, which the launch released.
TEST(session_retrigger_ignores_notes_of_the_previous_clip) {
  SessionSong s;
  Clip chord(s.a);
  chord.setLength(4);
  chord.getLeafPattern().setNote(0, 0, Note(110, 100));
  chord.getLeafPattern().setNote(0, 1, Note(111, 100));
  s.song.addClip(move(chord)); // clip 1
  Clip retrig(s.a);
  retrig.setLength(4);
  retrig.getLeafPattern().setNote(0, 0, Note(120, 100));
  for (int row = 0; row < 4; row++) retrig.getLeafPattern().setCommand(row, Command("-R03"));
  s.song.addClip(move(retrig)); // clip 2

  s.queue(s.a, 1);
  s.play(4); // clip 1's bar
  CHECK(s.plays(s.a, 111));
  s.queue(s.a, 2);
  s.play(4); // clip 2's bar, retriggering every row
  CHECK(s.plays(s.a, 120));
  CHECK(!s.plays(s.a, 111));
}

TEST(session_launch_waits_for_the_arrangements_marker_bars) {
  SessionSong s;
  s.song.setTimeSignatureMarker(8, { 2, 4 }); // bars of 4 rows until row 8, then 8
  s.play(10); // rows 0-9
  s.queue(s.a, 0);
  s.play(2); // rows 10-11: row 12 would be a bar of the old 4-row grid
  CHECK(s.plays(s.a, 51));
  CHECK(!s.plays(s.a, 100));
  s.play(4); // rows 12-15
  CHECK(!s.plays(s.a, 100));
  s.play(1); // row 16: the next bar of the 8-row grid
  CHECK(s.plays(s.a, 100));
}

TEST(session_launch_waits_for_the_transports_own_bars) {
  SessionSong s;
  s.song.setTransportBars({ 2, 4 }, 4); // 8-row bars counted from row 4: bars at 4 and 12
  s.play(5); // rows 0-4
  s.queue(s.a, 0);
  s.play(4); // rows 5-8: row 8 is a bar start of the arrangement's grid, not of the transport's
  CHECK(s.plays(s.a, 48));
  CHECK(!s.plays(s.a, 100));
  s.play(4); // rows 9-12: row 12 is the transport's bar
  CHECK(s.plays(s.a, 100));
}
