#include "TestFramework.h"

#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/SceneFollow.h"
#include "../src/model/Song.h"
#include "../src/state/ActiveVoiceInfo.h"
#include "../src/state/LiveTrackInfo.h"
#include "../src/state/SongState.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

using namespace std;

namespace {

// Scene 0: 3/8 at 120 BPM, a clip of 8 rows; scene 1: 5/8 at 90 BPM, clips of
// 6 and 12 rows (the scene is 12 long); scene 2: 2/4 at 150 BPM, 8 rows. Their
// ends (8, 20, 28) fall off every bar. Track a's clip row k of scene s plays
// note 20 + 30 * s + k.
struct FollowSong {
  Song song;
  int a = -1, b = -1;
  ChannelConfiguration config{44100, 1};
  unique_ptr<Mixer> mixer;
  unique_ptr<SongState> state;
  int seq = 0;

  explicit FollowSong(FollowAction first = FollowAction::NEXT, FollowAction last = FollowAction::NEXT) {
    song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
    a = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    b = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    const int lengths[3] = {8, 6, 8};
    for (int s = 0; s < 3; s++) {
      Clip clip(a);
      clip.setLength(lengths[s]);
      for (int k = 0; k < lengths[s]; k++) clip.getLeafPattern().setNote(k, 0, Note(20 + 30 * s + k, 100));
      song.addClip(move(clip));
    }
    song.ensureClipAt(b, 0);
    Clip long_clip(b);
    long_clip.setLength(12);
    long_clip.getLeafPattern().setNote(0, 0, Note(5, 100));
    song.addClip(move(long_clip));
    song.setSceneTempo(0, 120);
    song.setSceneTimeSignature(0, {3, 8});
    song.setSceneTempo(1, 90);
    song.setSceneTimeSignature(1, {5, 8});
    song.setSceneTempo(2, 150);
    song.setSceneTimeSignature(2, {2, 4});
    song.setSceneFollowAction(0, first);
    song.setSceneFollowAction(1, first);
    song.setSceneFollowAction(2, last);

    mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    state = make_unique<SongState>(config);
    state->initialize(song);
    state->setIsPlaying(true);
  }

  // Launches a scene the way ClipPlayer::launchScene() does.
  void launch(int scene, bool immediate = false) {
    state->queueSceneChange(song.getSceneTempo(scene), song.getSceneTimeSignature(scene), false, immediate, ++seq, scene);
    for (auto track : {a, b}) state->queueLaunch(track, scene, ++seq);
  }

  void block() { state->renderBlock(64, song, *mixer); }

  bool plays(int track_id, int value) const {
    unordered_map<int, vector<ActiveVoiceInfo> > voices;
    state->getAllActiveVoices(voices);
    auto it = voices.find(track_id);
    if (it == voices.end()) return false;
    for (auto & voice : it->second)
      if (voice.note_value == value) return true;
    return false;
  }

  // Plays until `clock` rows have been played (at most `limit` blocks).
  void playUntilClock(int clock, int limit = 200000) {
    for (int i = 0; i < limit && state->getLiveClock() < clock && state->isPlaying(); i++) block();
  }
};

} // namespace

TEST(follow_decision_for_each_action) {
  using followaction::decide;
  using followaction::FollowResult;
  CHECK(decide(FollowAction::NONE, 0, 3).kind == FollowResult::NOTHING);
  CHECK(decide(FollowAction::STOP, 0, 3).kind == FollowResult::STOP);
  auto next = decide(FollowAction::NEXT, 0, 3);
  CHECK(next.kind == FollowResult::LAUNCH && next.scene == 1);
  CHECK(decide(FollowAction::NEXT, 2, 3).kind == FollowResult::STOP); // no wrap
  CHECK(decide(FollowAction::AGAIN, 0, 3).kind == FollowResult::NOTHING);
  CHECK(decide(FollowAction::JUMP, 0, 3).kind == FollowResult::NOTHING);
}

TEST(follow_next_plays_scenes_back_to_back_and_the_last_stops) {
  FollowSong s;
  s.launch(0, true);
  const int starts[3] = {0, 8, 20};
  const int tempos[3] = {120, 90, 150};
  const int signatures[3] = {3, 5, 2};
  int last_clock = -1;
  int blocks_in_row = 0;
  while (s.state->isPlaying() && s.state->getLiveClock() < 100) {
    s.block();
    auto clock = s.state->getLiveClock();
    if (!s.state->isPlaying()) break;
    blocks_in_row = clock == last_clock ? blocks_in_row + 1 : 1;
    last_clock = clock;
    // The block that ends a row only moves the clock; the next one plays the new row.
    if (blocks_in_row != 2) continue;
    int scene = clock >= 20 ? 2 : clock >= 8 ? 1
                                             : 0;
    auto live = s.state->getLiveTracks().find(s.a);
    CHECK(live != s.state->getLiveTracks().end());
    if (live == s.state->getLiveTracks().end()) break;
    CHECK(live->second.clip_index == scene); // never idle or silent at a join
    CHECK(s.state->getTempo() == tempos[scene]);
    CHECK(s.state->getRunningBars().signature.numerator == signatures[scene]);
    CHECK(s.state->getRunningBars().origin == starts[scene]);
    int k = clock - starts[scene];
    int length = scene == 1 ? 6 : 8;
    CHECK(s.plays(s.a, 20 + 30 * scene + k % length)); // sounding from the first block of the row
  }
  CHECK(!s.state->isPlaying());
  CHECK(s.state->getLiveClock() == 28); // 8 + 12 + 8
  CHECK(s.state->getSceneFollowSeq() == 2);
}

TEST(follow_stop_releases_what_sounds_and_stops_the_transport) {
  FollowSong s(FollowAction::STOP);
  s.launch(0, true);
  s.playUntilClock(100);
  CHECK(!s.state->isPlaying());
  CHECK(s.state->getLiveClock() == 8);
  CHECK(s.state->getSceneFollowSeq() == 0);
}

TEST(no_follow_action_plays_as_before) {
  FollowSong s(FollowAction::NONE, FollowAction::NONE);
  s.launch(0, true);
  s.playUntilClock(40);
  CHECK(s.state->isPlaying());
  CHECK(s.state->getLiveTracks().at(s.a).clip_index == 0); // the scene loops until told otherwise
  CHECK(s.state->getTempo() == 120);
}

TEST(manual_scene_launch_cancels_the_pending_follow_action) {
  FollowSong s;
  s.launch(0, true);
  s.playUntilClock(7);
  s.launch(2); // queued for scene 0's next bar, after its follow time
  bool saw_scene_1 = false;
  while (s.state->isPlaying() && s.state->getLiveClock() < 100) {
    s.block();
    auto live = s.state->getLiveTracks().find(s.a);
    if (live != s.state->getLiveTracks().end() && live->second.clip_index == 1) saw_scene_1 = true;
  }
  CHECK(!saw_scene_1);
  CHECK(s.state->getLiveClock() == 20); // scene 2 started on scene 0's bar at row 12 and ran its 8 rows
}

TEST(follow_attributes_survive_load_and_save) {
  namespace fs = std::filesystem;
  auto path = (fs::path(TESTS_SCRATCH_DIR) / "scene_follow_scratch.xml").string();
  {
    ofstream out(path);
    out << "<song><scenes>"
           "<scene name=\"A\" followAction=\"next\"/>"
           "<scene name=\"B\" followAction=\"again\" followActionB=\"stop\" followActionChanceA=\"70\" followActionChanceB=\"30\" "
           "followActionTime=\"1.2.0\" followActionJumpTarget=\"A\" followActionEnabled=\"false\"/>"
           "<scene name=\"C\" followAction=\"bogus\"/>"
           "<scene name=\"D\"/>"
           "</scenes></song>";
  }
  InstrumentProvider provider;
  Song loaded;
  CHECK(loaded.open(path, provider));
  CHECK(loaded.getSceneFollowAction(0) == FollowAction::NEXT);
  CHECK(loaded.getSceneFollowAction(1) == FollowAction::AGAIN); // kept, acts as none
  CHECK(loaded.getSceneFollowAction(2) == FollowAction::NONE);
  CHECK(loaded.getSceneFollowAction(3) == FollowAction::NONE);
  loaded.save(path);
  ifstream in(path);
  stringstream text;
  text << in.rdbuf();
  auto saved = text.str();
  for (auto expected : {"followAction=\"next\"", "followAction=\"again\"", "followActionB=\"stop\"", "followActionChanceA=\"70\"", "followActionChanceB=\"30\"", "followActionTime=\"1.2.0\"",
                        "followActionJumpTarget=\"A\"", "followActionEnabled=\"false\""})
    CHECK(saved.find(expected) != string::npos);
  CHECK(saved.find("bogus") == string::npos);
  Song again;
  CHECK(again.open(path, provider));
  CHECK(again.getSceneFollowAction(0) == FollowAction::NEXT);
  CHECK(again.getSceneReservedAttribute(1, "followActionTime") == "1.2.0");
  CHECK(again.getSceneReservedAttribute(1, "followActionEnabled") == "false");
}
