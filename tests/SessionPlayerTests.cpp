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

TEST(session_player_launch_into_silence_waits_for_the_next_bar) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.player().tick(); // the clock starts at step 0
  f.advanceRows(1);

  f.player().triggerClip(track, 0);
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::QUEUED);
  f.advanceRows(2); // steps 2 and 3
  CHECK(SessionFixture::notesOn(f.drain(), track).empty());

  f.advanceRows(1); // step 4 - the bar
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{60}));
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::PLAYING);

  f.advanceRows(5);
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{61, 62, 63, 60, 61}));
  auto playheads = f.player().playheads();
  CHECK(playheads[track].clip_index == 0);
  CHECK(playheads[track].row == 1);
}

TEST(session_player_launch_before_the_clock_runs_starts_on_its_step_zero) {
  SessionFixture f;
  auto track = f.addTrack(1);

  f.player().triggerClip(track, 0);
  CHECK(SessionFixture::notesOn(f.drain(), track).empty());
  f.player().tick();
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{60}));
}

TEST(session_player_clips_launched_on_different_bars_stay_in_phase) {
  SessionFixture f;
  auto first = f.addTrack(1);
  auto second = f.addTrack(1, 4, 80);

  f.player().triggerClip(first, 0);
  f.player().tick();
  f.advanceRows(1);
  f.player().triggerClip(second, 0);
  f.drain();

  f.advanceRows(3); // steps 2, 3, then the bar at 4
  auto fired = f.drain();
  CHECK((SessionFixture::notesOn(fired, first) == std::vector<int>{62, 63, 60}));
  CHECK((SessionFixture::notesOn(fired, second) == std::vector<int>{80}));
}

TEST(session_player_empty_slot_stops_at_the_bar) {
  SessionFixture f;
  auto track = f.addTrack(1);

  f.player().triggerClip(track, 0);
  f.player().tick();
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
}

TEST(session_player_repressing_a_playing_clip_relaunches_it_from_row_zero) {
  SessionFixture f;
  auto track = f.addTrack(1, 8);

  f.player().triggerClip(track, 0);
  f.player().tick();
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
  f.player().tick();
  f.advanceRows(1);

  f.player().launchScene(1, {first, second});
  f.advanceRows(3);
  auto fired = f.drain();
  CHECK((SessionFixture::notesOn(fired, first) == std::vector<int>{70}));
  CHECK((SessionFixture::notesOn(fired, second) == std::vector<int>{90}));
}

TEST(session_player_one_shot_clip_stops_after_its_length) {
  SessionFixture f;
  auto track = f.addTrack(1, 2);
  f.controller.getSong().getClips(track)[0].setLooping(false);

  f.player().triggerClip(track, 0);
  f.player().tick();
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
  f.player().tick();
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
  f.player().tick();
  f.player().triggerClip(second, 0); // queued for the next bar
  f.drain();

  f.player().silenceAll();
  auto fired = f.drain();
  CHECK(SessionFixture::stoppedAll(fired, first));
  CHECK(f.player().playheads().empty());
}

TEST(session_player_take_starts_at_the_bar_and_loops_back_on_the_bar_it_stops) {
  SessionFixture f;
  auto track = f.addTrack(0);
  f.controller.setClipGridFocused(true);
  f.controller.setClipGridCursor(track, 0);
  f.controller.sendCommand("toggle-record-arm");
  CHECK(f.controller.isTrackArmed(track));
  f.player().tick();
  f.advanceRows(1);

  f.player().triggerClip(track, 0); // an empty slot: a fresh take
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::RECORD_QUEUED);
  CHECK(!f.controller.isSessionRecording(track));
  f.advanceRows(3); // the bar at 4
  CHECK(f.controller.isSessionRecording(track));

  auto row = f.controller.ensureSessionRecordingClip(track, 4);
  CHECK(row == 0);
  f.controller.getSong().getClips(track)[0].getLeafPattern().setNote(0, 0, Note(64, 100, 0));
  f.player().triggerClip(track, 0); // the slot being recorded: stop the take
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::RECORD_STOPPING);
  f.drain();

  f.advanceRows(3); // steps 5-7
  CHECK(f.controller.isSessionRecording(track));
  f.advanceRows(1); // the bar at 8
  CHECK(!f.controller.isSessionRecording(track));
  CHECK(f.player().isLaunched(track));
  CHECK((SessionFixture::notesOn(f.drain(), track) == std::vector<int>{64}));
}
