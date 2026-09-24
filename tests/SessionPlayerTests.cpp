#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/PlaybackInfo.h"
#include "../src/playback/PlaybackControlEvent.h"
#include "../src/playback/SessionPlayer.h"

#include <chrono>
#include <memory>
#include <vector>

namespace {

struct Fired { PlaybackControlEvent::Type type; int track_id; int value; };

// A song at 150 BPM (a row is 0.1 s) with 4 rows per bar, driven by a
// hand-advanced clock.
struct SessionFixture {
  ChannelConfiguration config{44100, 1};
  Controller controller{config};
  SessionPlayer::Clock::time_point now{};

  SessionFixture() {
    controller.switchToBuffer(controller.freshBufferName());
    controller.getSong().setTempo(150);
    controller.getSong().setRowsPerBar(4);
    player().setTimeSource([this] { return now; });
  }

  SessionPlayer & player() { return controller.getSessionPlayer(); }

  // A track whose clips each play note base+row on every row.
  int addTrack(int num_clips, int length = 4, int base = 60) {
    auto & song = controller.getSong();
    auto track_id = song.addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
    for (int c = 0; c < num_clips; c++) {
      auto & clip = song.addClip(Clip(track_id));
      clip.setLength(length);
      for (int row = 0; row < length; row++) clip.getLeafPattern().setNote(row, 0, Note(base + c * 10 + row, 100, 0));
    }
    return track_id;
  }

  // Advances time by `rows` rows (plus a little, so each boundary is
  // crossed rather than landed on) and ticks once per row.
  void advanceRows(int rows) {
    for (int i = 0; i < rows; i++) {
      now += std::chrono::milliseconds(101);
      player().tick();
    }
  }

  std::vector<Fired> drain() {
    std::vector<Fired> fired;
    auto & queue = controller.getPlaybackEventQueue();
    while (queue.hasEvents()) {
      auto event = queue.pop();
      auto control = dynamic_cast<PlaybackControlEvent *>(event.get());
      if (!control) continue;
      fired.push_back({control->getType(), control->getParameter1(),
        control->getType() == PlaybackControlEvent::PLAY_NOTE ? control->getParameter3() : 0});
    }
    return fired;
  }

  // The notes played on `track_id` in `fired`, in order.
  static std::vector<int> notesOn(const std::vector<Fired> & fired, int track_id) {
    std::vector<int> notes;
    for (auto & f : fired) {
      if (f.type == PlaybackControlEvent::PLAY_NOTE && f.track_id == track_id) notes.push_back(f.value);
    }
    return notes;
  }

  static bool stoppedAll(const std::vector<Fired> & fired, int track_id) {
    for (auto & f : fired) {
      if (f.type == PlaybackControlEvent::STOP_ALL_NOTES && f.track_id == track_id) return true;
    }
    return false;
  }
};

} // namespace

TEST(session_player_launch_from_silence_plays_row_zero_at_once_and_loops) {
  SessionFixture f;
  auto track = f.addTrack(1);

  f.player().triggerClip(track, 0);
  CHECK(f.player().originStep() == 0);
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{60}));

  f.advanceRows(5);
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{61, 62, 63, 60, 61}));
  auto playheads = f.player().playheads();
  CHECK(playheads[track].clip_index == 0);
  CHECK(playheads[track].row == 1);
}

TEST(session_player_second_launch_waits_for_the_next_bar) {
  SessionFixture f;
  auto first = f.addTrack(1);
  auto second = f.addTrack(1, 4, 80);

  f.player().triggerClip(first, 0);
  f.advanceRows(1);
  f.drain();

  f.player().triggerClip(second, 0);
  CHECK(f.player().clipHighlight(second, 0) == SessionPadHighlight::QUEUED);
  CHECK(f.player().playheads()[second].queued_clip == 0);

  f.advanceRows(2); // steps 2 and 3 - still inside the first bar
  CHECK(SessionFixture::notesOn(f.drain(), second).empty());

  f.advanceRows(1); // step 4 - the bar boundary
  CHECK((SessionFixture::notesOn(f.drain(), second) == std::vector<int>{80}));
  CHECK(f.player().clipHighlight(second, 0) == SessionPadHighlight::PLAYING);
}

TEST(session_player_empty_slot_stops_at_the_bar_and_clears_the_grid) {
  SessionFixture f;
  auto track = f.addTrack(1);

  f.player().triggerClip(track, 0);
  f.advanceRows(1);
  f.drain();

  f.player().triggerClip(track, 5); // no clip there
  CHECK(f.player().playheads()[track].queued_clip == -1);
  f.advanceRows(2);
  CHECK(!SessionFixture::stoppedAll(f.drain(), track));

  f.advanceRows(1); // step 4
  auto fired = f.drain();
  CHECK(SessionFixture::stoppedAll(fired, track));
  CHECK(SessionFixture::notesOn(fired, track).empty());
  CHECK(!f.player().isLaunched(track));
  CHECK(f.player().originStep() == -1);
}

TEST(session_player_repressing_a_playing_clip_relaunches_it_from_row_zero) {
  SessionFixture f;
  auto track = f.addTrack(1, 8);

  f.player().triggerClip(track, 0);
  f.advanceRows(1);
  f.player().triggerClip(track, 0);
  CHECK(f.player().isLaunched(track)); // a launch never toggles
  f.drain();

  f.advanceRows(3); // steps 2, 3, then the bar at 4
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{62, 63, 60}));
}

TEST(session_player_scene_launches_every_track_together) {
  SessionFixture f;
  auto first = f.addTrack(2);
  auto second = f.addTrack(2, 4, 80);

  f.player().launchScene(1, {first, second});
  auto fired = f.drain();
  CHECK((SessionFixture::notesOn(fired, first) == std::vector<int>{70}));
  CHECK((SessionFixture::notesOn(fired, second) == std::vector<int>{90}));
}

TEST(session_player_one_shot_clip_stops_after_its_length) {
  SessionFixture f;
  auto track = f.addTrack(1, 2);
  f.controller.getSong().getClips(track)[0].setLooping(false);

  f.player().triggerClip(track, 0);
  f.advanceRows(2);
  auto fired = f.drain();
  CHECK((SessionFixture::notesOn(fired, track) == std::vector<int>{60, 61}));
  CHECK(SessionFixture::stoppedAll(fired, track));
  CHECK(!f.player().isLaunched(track));
}

TEST(session_player_clock_stops_while_the_transport_plays) {
  SessionFixture f;
  auto track = f.addTrack(1);

  f.player().triggerClip(track, 0);
  f.drain();

  PlaybackInfo info = f.controller.getPlaybackInfo();
  info.setIsPlaying(true);
  f.controller.setPlaybackInfo(info);
  f.advanceRows(3);
  CHECK(SessionFixture::notesOn(f.drain(), track).empty());
  CHECK(f.player().playheads()[track].row == -1);
}

TEST(session_player_silence_all_releases_and_forgets_everything) {
  SessionFixture f;
  auto first = f.addTrack(1);
  auto second = f.addTrack(1);

  f.player().triggerClip(first, 0);
  f.player().triggerClip(second, 0); // queued behind the first
  f.drain();

  f.player().silenceAll();
  auto fired = f.drain();
  CHECK(SessionFixture::stoppedAll(fired, first));
  CHECK(f.player().playheads().empty());
  CHECK(f.player().originStep() == -1);
}
