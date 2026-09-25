#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"
#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/state/PlaybackInfo.h"
#include "../src/state/SongState.h"
#include "../src/state/SessionTrackInfo.h"
#include "../src/state/ActiveVoiceInfo.h"
#include "../src/playback/PlaybackControlEvent.h"
#include "../src/playback/SessionPlayer.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

// A Controller with a stand-in for the audio thread: events the
// SessionPlayer queues reach a real SongState, which plays one row at a
// time and reports back through Controller::receivePlaybackSnapshot(), the
// way Player does. 4 rows per bar, one long section.
struct SessionFixture {
  ChannelConfiguration config{44100, 1};
  Controller controller{config};
  std::unique_ptr<Mixer> mixer;
  std::unique_ptr<SongState> state;

  SessionFixture() {
    controller.switchToBuffer(controller.freshBufferName());
    auto & song = controller.getSong();
    song.setRowsPerBar(4);
    song.addInstrument(std::make_unique<Oscillator>(WaveformType::SINE));
    song.getOrCreateSection(0).setLengthBars(16);
    mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    state = std::make_unique<SongState>(config);
    state->initialize(song);
  }

  SessionPlayer & player() { return controller.getSessionPlayer(); }
  Song & song() { return controller.getSong(); }

  // A track whose clips each play note base+10*clip+row on every row.
  int addTrack(int num_clips, int length = 4, int base = 60) {
    auto track_id = song().addTrack(std::make_unique<InstrumentTrack>(0)).getInternalId();
    for (int c = 0; c < num_clips; c++) {
      Clip clip(track_id);
      clip.setLength(length);
      for (int row = 0; row < length; row++) clip.getLeafPattern().setNote(row, 0, Note(base + c * 10 + row, 100, 0));
      song().addClip(std::move(clip));
    }
    return track_id;
  }

  void armForSessionRecording(int track_id) {
    controller.setClipGridFocused(true);
    controller.setClipGridCursor(track_id, 0);
    controller.sendCommand("toggle-record-arm");
  }

  // What Player does with each queued event.
  void applyEvents() {
    auto & queue = controller.getPlaybackEventQueue();
    while (queue.hasEvents()) {
      auto event = queue.pop();
      auto control = dynamic_cast<PlaybackControlEvent *>(event.get());
      if (!control) continue;
      switch (control->getType()) {
      case PlaybackControlEvent::PLAY: state->setIsPlaying(true); break;
      case PlaybackControlEvent::STOP: state->setIsPlaying(false); state->silenceSession(-1); break;
      case PlaybackControlEvent::QUEUE_SESSION_CHANGE: state->queueSessionChange(control->getParameter1(), control->getParameter2(), control->getParameter3()); break;
      case PlaybackControlEvent::SILENCE_SESSION: state->silenceSession(control->getParameter1()); break;
      default: break;
      }
    }
  }

  void sendSnapshot() {
    PlaybackInfo info;
    auto [ section_idx, row_idx ] = state->getRelativePosition(song());
    info.setIsPlaying(state->isPlaying());
    info.setSampleInterval(config.getSampleInterval(song().getTempo()));
    info.setSamplePos(state->getSamplePos());
    info.setPatternIdx(section_idx);
    info.setRowIdx(row_idx);
    info.setAbsolutePos(state->getAbsolutePosition());
    info.setPositionEditSeq(state->getPositionEditSeq());
    info.setSessionTracks(state->getSessionTracks());
    info.setSessionClock(state->getSessionClock());
    info.setSessionStartClock(state->getSessionStartClock());
    info.setSessionSeq(state->getSessionSeq());
    controller.receivePlaybackSnapshot(controller.getActiveBufferName(), info);
  }

  // Plays `rows` rows, the UI catching up after each one.
  void playRows(int rows) {
    for (int i = 0; i < rows; i++) {
      applyEvents();
      state->renderBlock(config.getSampleInterval(song().getTempo()), song(), *mixer);
      sendSnapshot();
      player().tick();
    }
  }

  bool plays(int track_id, int value) const {
    std::unordered_map<int, std::vector<ActiveVoiceInfo> > voices;
    state->getAllActiveVoices(voices);
    auto it = voices.find(track_id);
    if (it == voices.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), [&](auto & voice) { return voice.note_value == value; });
  }
};

} // namespace

TEST(session_player_launch_while_stopped_starts_the_transport_on_the_bar) {
  SessionFixture f;
  auto track = f.addTrack(1);

  f.player().triggerClip(track, 0);
  CHECK(f.controller.getPlaybackInfo().isPlaying());
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::QUEUED); // predicted before the audio thread has it
  f.playRows(1); // row 0 is a bar start
  CHECK(f.player().isLaunched(track));
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::PLAYING);
  CHECK(f.plays(track, 60));
}

TEST(session_player_launch_waits_for_the_next_bar) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.controller.togglePlaying();
  f.playRows(1);

  f.player().triggerClip(track, 0);
  f.playRows(3); // rows 1-3
  CHECK(!f.player().isLaunched(track));
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::QUEUED);
  f.playRows(1); // row 4, the bar
  CHECK(f.player().isLaunched(track));
  CHECK(f.plays(track, 60));
}

TEST(session_player_prediction_survives_a_stale_snapshot) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.controller.togglePlaying();
  f.playRows(1);

  f.player().triggerClip(track, 0);
  f.sendSnapshot(); // taken before the audio thread saw the launch
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::QUEUED);
  f.applyEvents();
  f.sendSnapshot();
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::QUEUED);
}

TEST(session_player_empty_slot_takes_the_track_over_until_it_returns) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.controller.togglePlaying();

  f.player().triggerClip(track, 5); // no clip there: a stop
  f.playRows(1);
  CHECK(f.player().isTakenOver(track));
  CHECK(!f.player().isLaunched(track));

  f.player().returnAllToArrangement();
  f.playRows(4); // the next bar
  CHECK(!f.player().isTakenOver(track));
}

TEST(session_player_scene_launches_every_track_together) {
  SessionFixture f;
  auto first = f.addTrack(2);
  auto second = f.addTrack(2, 4, 80);
  f.controller.togglePlaying();
  f.playRows(1);

  f.player().launchScene(1, {first, second});
  f.playRows(4); // rows 1-4, the bar
  CHECK(f.plays(first, 70));
  CHECK(f.plays(second, 90));
}

TEST(session_player_transport_stop_silences_launched_clips) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.player().triggerClip(track, 0);
  f.playRows(2);
  CHECK(f.player().isLaunched(track));

  f.controller.togglePlaying();
  f.playRows(1);
  CHECK(!f.player().isLaunched(track));
  CHECK(f.player().isTakenOver(track));
}

TEST(session_player_take_starts_at_the_bar_and_loops_back_on_the_bar_it_stops) {
  SessionFixture f;
  auto track = f.addTrack(0);
  f.armForSessionRecording(track);
  CHECK(f.controller.isTrackArmed(track));

  f.player().triggerClip(track, 0); // an empty slot: a fresh take, starting the transport
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::RECORD_QUEUED);
  f.playRows(1); // row 0, the bar
  CHECK(f.controller.isSessionRecording(track));

  auto step = f.player().quantizedStep();
  auto row = f.controller.ensureSessionRecordingClip(track, step.step, step.bar_start);
  CHECK(row == 1); // the take began on the bar at session clock 0
  f.song().getClips(track)[0].getLeafPattern().setNote(1, 0, Note(64, 100, 0));
  f.player().triggerClip(track, 0); // the slot being recorded: stop the take
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::RECORD_STOPPING);

  f.playRows(2); // rows 1-2
  CHECK(f.controller.isSessionRecording(track));
  f.playRows(1); // row 3, and the UI sees the bar at row 4 begin: the take ends
  CHECK(!f.controller.isSessionRecording(track));
  f.playRows(1); // row 4: the take's clip launches on that same bar
  CHECK(f.player().isLaunched(track));
  f.playRows(1);
  CHECK(f.plays(track, 64));
}

// Arming a track must not turn a launch into a recording: a populated slot
// only launches.
TEST(session_player_pad_press_on_an_armed_track_never_overdubs) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.armForSessionRecording(track);

  f.player().triggerClip(track, 0);
  f.playRows(1);
  CHECK(f.player().isLaunched(track));
  CHECK(!f.controller.isSessionRecording(track));
}

TEST(session_player_session_record_overdubs_the_playing_clip_without_restarting_it) {
  SessionFixture f;
  auto track = f.addTrack(1);
  f.player().triggerClip(track, 0);
  f.playRows(3);
  CHECK(f.player().isLaunched(track));

  CHECK(f.player().toggleOverdub(track));
  CHECK(f.player().clipHighlight(track, 0) == SessionPadHighlight::RECORD_QUEUED);
  for (int i = 0; i < 8 && !f.controller.isSessionRecording(track); i++) f.playRows(1); // until the next bar
  CHECK(f.controller.isSessionRecording(track));
  CHECK(f.controller.getSessionRecordingClipIndex(track) == 0);
  CHECK(f.player().isLaunched(track));
  CHECK(f.player().playheads().at(track).row == 0); // not restarted mid-loop: this is its own bar boundary

  // A second press stops the take at the next bar; the clip keeps playing.
  CHECK(f.player().toggleOverdub(track));
  for (int i = 0; i < 8 && f.controller.isSessionRecording(track); i++) f.playRows(1);
  CHECK(!f.controller.isSessionRecording(track));
  CHECK(f.player().isLaunched(track));
}

TEST(session_player_session_record_with_nothing_playing_does_nothing) {
  SessionFixture f;
  auto track = f.addTrack(1);
  CHECK(!f.player().toggleOverdub(track));
  CHECK(!f.controller.isSessionRecording(track));
}
