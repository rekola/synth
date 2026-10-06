#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/SampleContent.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/LeafTrack.h"
#include "../src/model/PercussionTrack.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/Clip.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/PlaybackInfo.h"
#include "../src/state/TrackInfo.h"
#include "../src/playback/PlaybackControlEvent.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

namespace {

std::string readFile(const std::string & path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

TEST(controller_save_song_writes_to_the_opened_path) {
  // save-song used to always write "tmp.xml" regardless of which song was
  // open; it must write back to the file that was actually opened.
  namespace fs = std::filesystem;
  auto scratch_path = fs::path(TESTS_SCRATCH_DIR) / "controller_save_song_scratch.xml";
  fs::copy_file(fs::path(TESTS_FIXTURES_DIR) / "center_note.xml", scratch_path,
		fs::copy_options::overwrite_existing);

  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  CHECK(controller.openSong(scratch_path.string()));
  CHECK(controller.getActiveBufferName() == scratch_path.string());

  // mutate the song so the saved file is distinguishable from the original
  controller.getSong().setTempo(200);

  CHECK(controller.sendCommand("save-song"));

  auto saved = readFile(scratch_path.string());
  CHECK(saved.find("tempo=\"200\"") != std::string::npos);

  fs::remove(scratch_path);
  CHECK(!fs::exists("tmp.xml")); // the old hardcoded destination must not appear
}

TEST(controller_switch_to_fresh_buffer_leaves_previous_buffer_open) {
  // No separate "new song" command exists any more (see Controller.h's
  // switchToBuffer() comment) - switchToBuffer(freshBufferName()) is what
  // replaced it, and unlike the old createNewSong() it must not discard
  // the buffer that was active before: real multi-buffer support means
  // both stay open.
  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  auto fixture = std::string(TESTS_FIXTURES_DIR) + "/center_note.xml";
  CHECK(controller.openSong(fixture));
  CHECK(controller.getActiveBufferName() == fixture);

  controller.switchToBuffer(controller.freshBufferName());
  CHECK(controller.getActiveBufferName() != fixture);

  // The fixture is still open, just no longer active.
  auto names = controller.getBufferNames();
  CHECK(std::find(names.begin(), names.end(), fixture) != names.end());
}

TEST(controller_per_buffer_state_survives_switching_away_and_back) {
  // playback_info/recording_track_id are each a live scalar mirroring
  // whichever buffer is active, swapped against a per-buffer map on every
  // switchToBuffer() (see Controller.h's save/loadActiveBufferState()) -
  // switching away must not lose one buffer's state, and switching back
  // must restore it rather than leaving the other buffer's state behind.
  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  auto buffer_a = controller.freshBufferName();
  controller.switchToBuffer(buffer_a);
  controller.setRecordingTrackId(3);
  PlaybackInfo info_a;
  info_a.setAbsolutePos(5);
  controller.setPlaybackInfo(info_a);

  auto buffer_b = controller.freshBufferName();
  controller.switchToBuffer(buffer_b);
  // A brand-new buffer must start out with fresh, default state, not
  // buffer_a's - not merely leftover live-scalar values.
  CHECK(controller.getRecordingTrackId() == 0);
  CHECK(controller.getPlaybackInfo().getAbsolutePosition() == 0);

  controller.setRecordingTrackId(7);
  PlaybackInfo info_b;
  info_b.setAbsolutePos(9);
  controller.setPlaybackInfo(info_b);

  controller.switchToBuffer(buffer_a);
  CHECK(controller.getRecordingTrackId() == 3);
  CHECK(controller.getPlaybackInfo().getAbsolutePosition() == 5);

  controller.switchToBuffer(buffer_b);
  CHECK(controller.getRecordingTrackId() == 7);
  CHECK(controller.getPlaybackInfo().getAbsolutePosition() == 9);
}

// Regression: receivePlaybackSnapshot()'s staleness check compares a
// snapshot's PlaybackInfo::getPositionEditSeq() against Controller's own
// local_position_edit_seq_ - per-buffer, same swap-on-switch shape as
// playback_info itself (see Controller.h's own comment on why). Before
// this was per-buffer, navigating one buffer ran its shared counter ahead
// of a different, freshly-live buffer's own (always-starts-at-0)
// SongState counter, so every real snapshot for that other buffer looked
// permanently stale and its displayed position never updated.
TEST(controller_position_edit_seq_does_not_leak_across_buffers) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  auto buffer_a = controller.freshBufferName();
  controller.switchToBuffer(buffer_a);
  // Plenty of navigation on buffer_a, driving its own edit-seq counter
  // well past what a brand-new buffer's live SongState would ever start
  // at (see Player::stateFor()/setPositionWithEditSeq()).
  for (int i = 0; i < 10; i++) controller.moveEditPosition(1);

  auto buffer_b = controller.freshBufferName();
  controller.switchToBuffer(buffer_b);

  // A real Player-thread snapshot for buffer_b, as if its own live
  // SongState had just been constructed and advanced one row - must be
  // accepted as fresh, not folded back to buffer_b's (default, row 0)
  // local mirror.
  PlaybackInfo snapshot;
  snapshot.setAbsolutePos(5);
  snapshot.setPositionEditSeq(1);
  controller.receivePlaybackSnapshot(buffer_b, snapshot);

  CHECK(controller.getPlaybackInfo().getAbsolutePosition() == 5);
}

// syncLiveGlideStateIntoModel() - receivePlaybackSnapshot()'s own
// model-sync half (Controller::glideTrackSendA()/etc.'s own comment on
// why the model isn't written at press time any more): a snapshot
// carrying a track's real, engine-reported live Send A (TrackInfo::
// getLiveSendA(), the same field LeafTrackState::renderVoices()
// populates) must be mirrored into that track's own model-layer
// LeafTrack, not just held in getPlaybackInfo() - so anything reading the
// model (Launchpad's own LED refresh included) sees what the engine
// actually did.
TEST(receive_playback_snapshot_syncs_a_tracks_live_send_into_the_model) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & track = dynamic_cast<LeafTrack &>(controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0)));
  auto track_id = track.getInternalId();
  CHECK(track.getSends().a == 0.0f); // the track's own untouched default

  std::unordered_map<int, TrackInfo> track_info;
  track_info[track_id] = TrackInfo(true, false, -1.0f, 1.0f, 0.42f, 1.0f); // live_send_a = 0.42f
  PlaybackInfo snapshot;
  snapshot.setTrackInfo(std::move(track_info));

  controller.receivePlaybackSnapshot(buffer_name, snapshot);

  CHECK_NEAR(track.getSends().a, 0.42f, 1e-6f);
}

// A track this session's live engine has never actually rendered (no
// TrackInfo entry for it at all in the snapshot) must be left alone -
// TrackInfo::hasLiveSends()'s own -1.0f "not reported" sentinel is what
// tells "genuinely reported 0" apart from "no live data yet", and a
// snapshot missing a track entirely must not silently zero its model.
TEST(receive_playback_snapshot_leaves_a_track_with_no_live_data_untouched) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & track = dynamic_cast<LeafTrack &>(controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0)));
  track.setSendA(0.7f); // a real, hand-set value - not the default

  PlaybackInfo snapshot; // no TrackInfo entries at all
  controller.receivePlaybackSnapshot(buffer_name, snapshot);

  CHECK_NEAR(track.getSends().a, 0.7f, 1e-6f);
}

// syncLiveGlideStateIntoModel()'s own azimuth half - Pan's live glide
// moved server-side the same way Send Main/A/B did, so it needs the
// identical engine-value -> model mirroring (TrackInfo::getLiveAzimuth(),
// LeafTrackState::renderVoices()'s own population of it).
TEST(receive_playback_snapshot_syncs_a_tracks_live_azimuth_into_the_model) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & track = dynamic_cast<LeafTrack &>(controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0)));
  auto track_id = track.getInternalId();
  CHECK(track.getAzimuth() == 0.0f); // the track's own untouched default

  std::unordered_map<int, TrackInfo> track_info;
  track_info[track_id] = TrackInfo(true, false, -1.0f, 1.0f, 1.0f, 1.0f, 42.0f, true); // live_azimuth = 42 degrees
  PlaybackInfo snapshot;
  snapshot.setTrackInfo(std::move(track_info));

  controller.receivePlaybackSnapshot(buffer_name, snapshot);

  CHECK_NEAR(track.getAzimuth(), 42.0f, 1e-6f);
}

// TrackInfo::hasLiveAzimuth()'s own bool flag (unlike the three Sends'
// shared -1.0f "not reported" sentinel - see its own comment on why
// azimuth needs a separate flag) is what tells "genuinely reported 0
// degrees" apart from "no live data yet" here.
TEST(receive_playback_snapshot_leaves_a_tracks_azimuth_untouched_with_no_live_data) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & track = dynamic_cast<LeafTrack &>(controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0)));
  track.setAzimuth(15.0f); // a real, hand-set value - not the default

  PlaybackInfo snapshot; // no TrackInfo entries at all
  controller.receivePlaybackSnapshot(buffer_name, snapshot);

  CHECK_NEAR(track.getAzimuth(), 15.0f, 1e-6f);
}

TEST(controller_disambiguates_buffers_sharing_a_basename) {
  // Emacs-style uniquify (Controller::getBufferDisplayName()): two open
  // buffers named "song.xml" in different directories must not display
  // identically in the Buffers menu/status bar - each needs just enough
  // of its own parent directory appended to tell them apart.
  namespace fs = std::filesystem;
  auto dir_a = fs::path(TESTS_SCRATCH_DIR) / "uniquify_a";
  auto dir_b = fs::path(TESTS_SCRATCH_DIR) / "uniquify_b";
  fs::create_directories(dir_a);
  fs::create_directories(dir_b);
  auto path_a = (dir_a / "song.xml").string();
  auto path_b = (dir_b / "song.xml").string();
  auto fixture = fs::path(TESTS_FIXTURES_DIR) / "center_note.xml";
  fs::copy_file(fixture, path_a, fs::copy_options::overwrite_existing);
  fs::copy_file(fixture, path_b, fs::copy_options::overwrite_existing);

  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  CHECK(controller.openSong(path_a));
  // Only one buffer open yet - no collision, no disambiguation needed.
  CHECK(controller.getBufferDisplayName(path_a) == "song.xml");

  CHECK(controller.openSong(path_b));
  // Now both share a basename - each shows its own parent directory.
  CHECK(controller.getBufferDisplayName(path_a) == "song.xml<uniquify_a>");
  CHECK(controller.getBufferDisplayName(path_b) == "song.xml<uniquify_b>");

  fs::remove(path_a);
  fs::remove(path_b);
  fs::remove(dir_a);
  fs::remove(dir_b);
}

TEST(controller_send_command_prefers_literal_commands_over_fallback) {
  // The M-x path (StatusLine -> Controller::sendCommand) must keep working
  // for Controller's own literal commands even when a UI-supplied fallback
  // is installed (e.g. UI::executeCommand, wired for per-widget commands
  // like "set-mark") - the fallback should only be consulted for names
  // Controller doesn't recognize itself.
  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  int fallback_calls = 0;
  std::string last_fallback_name;
  controller.setCommandFallback([&](std::string_view name) {
    fallback_calls++;
    last_fallback_name = std::string(name);
    return name == "set-mark"; // simulates a widget recognizing this one
  });

  CHECK(controller.sendCommand("add-filter")); // literal Controller command
  CHECK(fallback_calls == 0); // must not have consulted the fallback

  CHECK(controller.sendCommand("set-mark")); // not a literal command
  CHECK(fallback_calls == 1);
  CHECK(last_fallback_name == "set-mark");

  CHECK(!controller.sendCommand("totally-bogus-command"));
  CHECK(fallback_calls == 2);
}

TEST(controller_command_completions_merges_literal_and_fallback_names) {
  // The read-only sibling of the test above: commandCompletions() is what
  // StatusLine's M-x autocomplete queries, and it must see both Controller's
  // own literal commands and whatever a UI-supplied completer (e.g.
  // UI::commandCompletions, wired for per-widget commands) reaches.
  ChannelConfiguration config(44100, 1);
  Controller controller(config);

  controller.setCommandCompleter([](std::string_view prefix) {
    std::set<std::string> result;
    for (std::string_view name : { "set-mark", "save-song-as" }) {
      if (name.substr(0, prefix.size()) == prefix) result.emplace(name);
    }
    return result;
  });

  auto matches = controller.commandCompletions("save-song");
  CHECK(matches.count("save-song") == 1); // Controller's own literal command
  CHECK(matches.count("save-song-as") == 1); // reached via the fallback completer
  CHECK(matches.count("set-mark") == 0); // doesn't share the "save-song" prefix

  auto all = controller.commandCompletions("");
  CHECK(all.count("add-filter") == 1);
  CHECK(all.count("toggle-mixer-type") == 1);
  CHECK(all.count("set-mark") == 1);
}

// A clip focus is a single, exclusive "what am I currently looking at to
// edit" pointer, not Session-view style multi-track launching - setting a
// new one must silence whatever the *previous* focus was actively
// previewing (a plain STOP_ALL_NOTES, LaunchpadManager::
// triggerAuditionStep()'s own target), even when that was on a different
// track, so at most one track's worth of preview audio is ever sounding
// at once.
TEST(focus_change_silences_the_previous_focused_tracks_preview) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & queue = controller.getPlaybackEventQueue();

  CHECK(!queue.hasEvents()); // nothing pushed yet, before any focus exists

  controller.setFocusedClip(3, "clip-a"); // first-ever focus - nothing to silence
  CHECK(controller.getFocusedClipTrackId() == 3);
  CHECK(controller.getFocusedClip() == "clip-a");

  controller.setFocusedClip(7, "clip-b"); // a different track's clip
  CHECK(queue.hasEvents());
  // Held in its own unique_ptr, not
  // chained straight into dynamic_cast(...pop().get()) - pop()'s own
  // return value is a temporary that would otherwise be destroyed (along
  // with the Event it owns) at the end of that one statement, leaving the
  // raw pointer dangling for every access after it.
  auto ev1_ptr = queue.pop();
  auto ev1 = dynamic_cast<PlaybackControlEvent *>(ev1_ptr.get());
  CHECK(ev1 != nullptr);
  CHECK(ev1->getType() == PlaybackControlEvent::STOP_ALL_NOTES);
  CHECK(ev1->getParameter1() == 3); // the *previous* focus's own track, not the new one
  CHECK(!queue.hasEvents()); // exactly one event, no more
  CHECK(controller.getFocusedClipTrackId() == 7);
  CHECK(controller.getFocusedClip() == "clip-b");

  controller.clearFocusedClip();
  auto ev2_ptr = queue.pop();
  auto ev2 = dynamic_cast<PlaybackControlEvent *>(ev2_ptr.get());
  CHECK(ev2 != nullptr);
  CHECK(ev2->getType() == PlaybackControlEvent::STOP_ALL_NOTES);
  CHECK(ev2->getParameter1() == 7);
  CHECK(!queue.hasEvents());
  CHECK(controller.getFocusedClipTrackId() == -1);
  CHECK(controller.getFocusedClip().empty());
}

TEST(kill_active_buffer_closes_the_song_and_switches_to_another) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto other = controller.freshBufferName();
  controller.switchToBuffer(other); // a second song, so the one under test isn't the app's only buffer

  auto name = controller.freshBufferName();
  controller.switchToBuffer(name);
  CHECK(controller.killActiveBuffer());

  auto names = controller.getBufferNames();
  CHECK(std::find(names.begin(), names.end(), name) == names.end());
  CHECK(controller.getActiveBufferName() == other); // switched to the only buffer left
}

TEST(kill_active_buffer_refuses_the_only_open_buffer) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto name = controller.freshBufferName();
  controller.switchToBuffer(name);
  CHECK(controller.getBufferNames().size() == 1);
  CHECK(!controller.killActiveBuffer());
  CHECK(controller.getActiveBufferName() == name); // untouched
}

// beginSampleCapture() is lazy - called only once real audio has arrived
// (UI::handleRecordEvent()'s own guard) - so a take that captured nothing
// at all (no capture device, or stopped again before a first block
// landed) never calls it, and finishSampleCapture() must have nothing to
// finalize or clean up either.
TEST(finish_sample_capture_with_no_audio_captured_is_a_clean_no_op) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  controller.setRecordingTrackId(track.getInternalId());
  controller.startRecording(); // a fresh, still-0-frame current_sample

  CHECK(!controller.hasRecordingClip());
  controller.finishSampleCapture();
  CHECK(!controller.hasRecordingClip());
  CHECK(!controller.isRecording());
  CHECK(controller.getSong().getClips(track.getInternalId()).empty());
}

// The real lifecycle: beginSampleCapture() creates a clip sharing
// current_sample's own buffer (so a later addToSample() is visible
// through it automatically), finishSampleCapture() finalizes its length
// from the real captured frame count.
TEST(begin_and_finish_sample_capture_creates_and_finalizes_a_real_clip) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTempo(120);

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  AudioBuffer block(1, 400);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 400; i++) data[i] = 0.3f;

  controller.addToSample(block); // real audio arrives...
  controller.beginSampleCapture(track_id); // ...only now does a clip get created
  CHECK(controller.hasRecordingClip());
  CHECK(controller.getSong().getClips(track_id).size() == 1);

  controller.addToSample(block); // a second block of the same take
  controller.finishSampleCapture();

  CHECK(!controller.hasRecordingClip());
  CHECK(!controller.isRecording());
  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  if (!clips.empty()) {
    auto & content = clips[0].getSampleContent();
    if (content.getBuffer()) {
      // Both blocks - the same shared_ptr addToSample() appends into.
      CHECK(content.getBuffer()->numberOfFrames() == 800);
    }
    CHECK(content.getOriginalTempo() == 120);
    CHECK(clips[0].getLength() > 0); // finalized from the real captured duration
  }
}

// Recording-latency compensation: armRecordingStart() snapshots where a
// take starts, synchronously - beginSampleCapture() places the clip
// there (not wherever getPlaybackInfo() might report by the time it
// actually runs), trims latency_frames off the in-point, and
// finishSampleCapture() subtracts that same amount from the final length
// so the lead-in never inflates the instance's own arrangement window.
TEST(armed_sample_capture_places_at_the_snapshotted_row_and_trims_the_measured_latency) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTempo(120);
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  controller.armRecordingStart(2); // row 2 - the take's own real start
  CHECK(controller.isRecordingArmed());

  AudioBuffer block(1, 800);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 800; i++) data[i] = 0.3f;
  controller.addToSample(block);

  controller.beginSampleCapture(track_id, 100); // 100 frames of measured round-trip latency
  CHECK(controller.hasRecordingClip());

  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  if (!clips.empty()) {
    auto & content = clips[0].getSampleContent();
    CHECK_NEAR(content.getInPoint(), 100.0f / 8000.0f, 1e-6f);
  }

  // Placed at the snapshotted row (2), not row 0 - resolveInstanceAt()
  // only finds it active there.
  auto active_at_start = resolveInstanceAt(controller.getSong(), track_id, 2);
  CHECK(active_at_start.clip_index == 0);
  auto active_at_zero = resolveInstanceAt(controller.getSong(), track_id, 0);
  CHECK(active_at_zero.clip_index == Arrangement::kNoInstance);

  controller.finishSampleCapture();
  CHECK(!controller.isRecordingArmed()); // reset back to unarmed for the next take

  auto & clips2 = controller.getSong().getClips(track_id);
  if (!clips2.empty()) {
    // 800 captured frames, 100 of them the lead-in - post-trim length
    // covers 700, not the full 800.
    auto expected_rows = config.framesToRows(700, 120);
    CHECK(clips2[0].getLength() == expected_rows);
  }
}

// The SampleTrack twin of ensure_session_recording_clip_creates_and_
// grows_at_the_exact_pressed_index (ControllerTests.cpp above) - a
// Session View take pressed at clip index 2 on a track with no clips at
// all yet has to land exactly there, holes and all, not collapse to
// wherever a fresh append would happen to land.
TEST(session_recording_sample_capture_lands_at_the_exact_pressed_index_with_holes) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTempo(120);

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  // A pad press on the armed track is what does this in practice
  // (LaunchpadManager/Controller::toggle-record-arm's own SampleTrack
  // carve-out) - done directly here to test beginSampleCapture() in
  // isolation, the same way the note-based test above does.
  controller.armSessionTrackRecording(track_id, 2);

  AudioBuffer block(1, 400);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 400; i++) data[i] = 0.3f;
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);
  CHECK(controller.hasRecordingClip());

  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 3);
  CHECK(clips[0].isEmpty()); // backfilled filler
  CHECK(clips[1].isEmpty()); // backfilled filler
  CHECK(!clips[2].isEmpty());
  CHECK(!clips[2].getId().empty()); // a still-id-less filler got a real one once it received content
  CHECK(clips[2].getName() == "Take 3");

  controller.finishSampleCapture();
  CHECK(controller.getSong().getClips(track_id).size() == 3); // finalizing doesn't move or add anything
  CHECK(clips[2].hasSample());
}

// A second Session View take recorded into the same, already-populated
// slot overdubs it - a new SampleContent layer alongside the first
// (Clip.h's own sample_layers_ comment), not a replacement, and
// finishSampleCapture() rebuilds getMixedContent()'s own cache so the
// clip actually plays both layers summed, not just the original take.
TEST(a_second_take_into_an_already_recorded_slot_overdubs_rather_than_replaces) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTempo(120);

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);

  AudioBuffer block(1, 400);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 400; i++) data[i] = 0.3f;

  // First take - lands at index 0, exactly like any other fresh Session
  // View recording.
  controller.startRecording();
  controller.armSessionTrackRecording(track_id, 0);
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);
  controller.addToSample(block);
  controller.finishSampleCapture();

  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(clips[0].hasSample());
  CHECK(clips[0].getSampleLayers().size() == 1);
  auto first_take_length = clips[0].getLength();

  // A second take pressed into that same slot - beginSampleCapture()'s
  // own is_overdub detection (reuse_existing && clip.hasSample()) must
  // fire, appending rather than replacing.
  controller.startRecording();
  controller.armSessionTrackRecording(track_id, 0);
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);
  CHECK(clips[0].getSampleLayers().size() == 2); // the overdub layer, appended immediately
  controller.addToSample(block);
  controller.finishSampleCapture();

  CHECK(controller.getSong().getClips(track_id).size() == 1); // still one clip, not a second one
  CHECK(clips[0].getSampleLayers().size() == 2);
  CHECK(clips[0].getLength() >= first_take_length); // an overdub only ever grows the clip's own length
  // getMixedContent() now serves the real, rebuilt composite - not just
  // falling back to layer 0 - now that finishSampleCapture() has run.
  CHECK(&clips[0].getMixedContent() != &clips[0].getSampleContent());
}

// A real bug report: a captured take defaulted to Clip's own looping=true,
// so a placed instance kept looping and kept
// re-triggering the (short) recording over and over instead of playing
// once and stopping - same reasoning ensureNoteRecordingClip() already
// applies to a live note-recording take.
TEST(sample_capture_creates_a_non_looping_clip_by_default) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  AudioBuffer block(1, 400);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 400; i++) data[i] = 0.3f;
  controller.addToSample(block);

  controller.beginSampleCapture(track_id);
  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  if (!clips.empty()) CHECK(!clips[0].isLooping());
}

// A real bug report: an old stop marker sitting later in the track's own
// timeline survived a live take, because the take's own clip was left at
// length 0 for its whole duration (only set once finishSampleCapture()
// runs) - a non-looping clip with length 0 falls back to "1 row long," so
// the transport's own per-row scheduling treated the in-progress take as
// already expired almost immediately, live, long before finishSampleCapture()
// ever got a chance to sweep anything. extendRecordingSampleClipIfNeeded()
// keeps the clip's own length growing (and sweeping stale events in its
// path) throughout the take instead.
TEST(extend_recording_sample_clip_if_needed_grows_the_clip_and_clears_a_stale_stop) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  // An old stop marker, well ahead of where the new take starts - left
  // over from some earlier, unrelated arrangement edit.
  placeStopInstance(controller.getSong(), track_id, 20);

  controller.setRecordingTrackId(track_id);
  controller.startRecording();
  controller.armRecordingStart(0);

  AudioBuffer block(1, 400);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 400; i++) data[i] = 0.3f;
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);

  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(clips[0].getLength() == 4); // one bar's worth, right away - not 0

  // The old stop marker is still there - the take hasn't grown anywhere
  // near it yet.
  CHECK(resolveInstanceAt(controller.getSong(), track_id, 20).clip_index == Arrangement::kStopInstance);

  // The transport advances well past the take's own current (small)
  // reach, the same way real per-row playback would while recording
  // continues.
  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setAbsolutePos(20);
  controller.setPlaybackInfo(info);
  controller.extendRecordingSampleClipIfNeeded();

  CHECK(clips[0].getLength() > 4); // grew to keep ahead of the current row
  // The stale stop marker is gone, swept by the same placeClipInstance()
  // re-run extendRecordingClipsIfNeeded() already uses for note takes -
  // row 20 now resolves to the still-active recording clip instead.
  CHECK(resolveInstanceAt(controller.getSong(), track_id, 20).clip_index == 0);
}

// The other half: a take that never called armRecordingStart() at all
// (the lazy, uncompensated path UI::handleRecordEvent() falls back to)
// stays exactly as unplaced as before this Part - a real, visible clip,
// just not an arrangement instance anywhere.
TEST(unarmed_sample_capture_stays_unplaced) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();
  CHECK(!controller.isRecordingArmed()); // never armed - a freeform take

  AudioBuffer block(1, 400);
  auto data = block.getChannelData(0);
  for (int i = 0; i < 400; i++) data[i] = 0.3f;
  controller.addToSample(block);

  controller.beginSampleCapture(track_id); // no latency argument - matches UI::handleRecordEvent()'s own lazy call
  CHECK(controller.hasRecordingClip());

  auto active = resolveInstanceAt(controller.getSong(), track_id, 0);
  CHECK(active.clip_index == Arrangement::kNoInstance); // never placed anywhere

  controller.finishSampleCapture();
  auto & clips = controller.getSong().getClips(track_id);
  if (!clips.empty()) CHECK(clips[0].getLength() == config.framesToRows(400, controller.getSong().getTempo())); // no latency to trim off
}

// armThresholdRecording()/disarmThresholdRecording(): the user-facing
// arm/cancel pair a Launchpad's CC19 toggles between while nothing has
// triggered yet.
TEST(threshold_recording_arm_and_disarm_toggle_the_flag) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  CHECK(!controller.isThresholdArmed());
  controller.armThresholdRecording(track_id);
  CHECK(controller.isThresholdArmed());
  CHECK(controller.getRecordingTrackId() == track_id);

  controller.disarmThresholdRecording();
  CHECK(!controller.isThresholdArmed());
}

// clearThresholdArmed(): the audio-thread-triggered transition once input
// actually crosses the threshold - same flag flip as disarm, but reached
// from UI::handleThresholdRecordingTriggeredEvent() instead of a direct
// user cancel. getRecordingTrackId() survives the transition, since the
// genuine take that follows targets the same track that was armed.
TEST(threshold_recording_clear_ends_the_arm_phase_without_losing_the_target_track) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  controller.armThresholdRecording(track_id);
  CHECK(controller.isThresholdArmed());

  controller.clearThresholdArmed();
  CHECK(!controller.isThresholdArmed());
  CHECK(controller.getRecordingTrackId() == track_id);
}

// "toggle-record-arm": one command for every track type, dispatching on
// Song::getCurrentTrackId()'s own type. A SampleTrack gets the existing
// threshold-armed cycle.
TEST(toggle_record_arm_arms_and_disarms_a_sample_track) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  song.setCurrentTrackId(track_id);

  CHECK(!controller.isThresholdArmed());
  controller.sendCommand("toggle-record-arm");
  CHECK(controller.isThresholdArmed());
  CHECK(controller.getRecordingTrackId() == track_id);

  controller.sendCommand("toggle-record-arm");
  CHECK(!controller.isThresholdArmed());
}

// Every other track type gets the plain note-capture-armed toggle.
TEST(toggle_record_arm_arms_and_disarms_an_instrument_track) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  song.setCurrentTrackId(track.getInternalId());

  CHECK(!controller.isNoteCaptureArmed());
  controller.sendCommand("toggle-record-arm");
  CHECK(controller.isNoteCaptureArmed());
  CHECK(!controller.isThresholdArmed()); // the other track type's flag is untouched

  controller.sendCommand("toggle-record-arm");
  CHECK(!controller.isNoteCaptureArmed());
}

// Record Arm is one global state: whatever is already armed/recording
// always takes priority over arming something new, regardless of which
// track is currently selected - so switching to a different track while
// a take is in progress and pressing the button again still stops that
// take, not starts an unrelated second one.
TEST(toggle_record_arm_disarms_whatever_is_active_regardless_of_current_track) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & sample_track = song.addTrack(std::make_unique<SampleTrack>());
  auto sample_track_id = sample_track.getInternalId();
  auto & instrument_track = song.addTrack(std::make_unique<InstrumentTrack>(0));

  song.setCurrentTrackId(sample_track_id);
  controller.sendCommand("toggle-record-arm");
  CHECK(controller.isThresholdArmed());

  // Cursor moves to a different track while still armed - harmless.
  song.setCurrentTrackId(instrument_track.getInternalId());
  controller.sendCommand("toggle-record-arm");
  CHECK(!controller.isThresholdArmed()); // disarmed the SampleTrack take...
  CHECK(!controller.isNoteCaptureArmed()); // ...not armed the instrument track instead
}

TEST(toggle_record_arm_is_a_noop_with_no_current_track_set) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  CHECK(controller.getSong().getCurrentTrackId() == -1); // never set
  controller.sendCommand("toggle-record-arm");
  CHECK(!controller.isThresholdArmed());
  CHECK(!controller.isNoteCaptureArmed());
}

// A real bug report: stopping the transport left an in-progress take just
// hanging, with nothing left to record into - Space (togglePlaying(), the
// only way playback ever actually stops) needs to finish it, not just
// "toggle-record-arm" itself.
TEST(toggle_playing_finishes_an_in_progress_recording_when_transport_stops) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  AudioBuffer block(1, 400);
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);
  CHECK(controller.isRecording());
  CHECK(controller.hasRecordingClip());

  controller.togglePlaying(); // starts playing - nothing to tear down yet
  CHECK(controller.isRecording());

  controller.togglePlaying(); // Space - stops the transport
  CHECK(!controller.getPlaybackInfo().isPlaying());
  CHECK(!controller.isRecording());
  CHECK(!controller.hasRecordingClip());
}

// Same bug, the threshold-armed (not yet actually recording) case - arming
// a SampleTrack auto-starts the transport, so manually stopping it again
// needs to disarm the take that auto-start was for, not leave it armed
// with playback now stopped underneath it.
TEST(toggle_playing_disarms_threshold_recording_when_transport_stops) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  song.setCurrentTrackId(track.getInternalId());

  controller.sendCommand("toggle-record-arm"); // arms + auto-starts playback
  CHECK(controller.isThresholdArmed());
  CHECK(controller.getPlaybackInfo().isPlaying());

  controller.togglePlaying(); // Space - stops the transport directly, not via the button
  CHECK(!controller.getPlaybackInfo().isPlaying());
  CHECK(!controller.isThresholdArmed());
}

// Same bug, the note-capture-armed case.
TEST(toggle_playing_disarms_note_capture_when_transport_stops) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  song.setCurrentTrackId(track.getInternalId());

  controller.sendCommand("toggle-record-arm");
  CHECK(controller.isNoteCaptureArmed());
  controller.togglePlaying(); // starts playing (arming a note track doesn't do this itself here - that's LaunchpadManager's own job)
  CHECK(controller.getPlaybackInfo().isPlaying());

  controller.togglePlaying(); // Space - stops the transport
  CHECK(!controller.getPlaybackInfo().isPlaying());
  CHECK(!controller.isNoteCaptureArmed());
}

// Session View recording: arming with isClipGridFocused() true targets
// whatever slot setClipGridCursor() names, never the shared track
// cursor, and never auto-starts the transport (Player.cpp already starts
// ALSA capture off isThresholdArmed() alone) - so the resulting take stays
// entirely unplaced, populating only the clip slot.
TEST(toggle_record_arm_clip_grid_focused_arms_a_sample_track_without_arrangement_placement) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  auto & other_track = song.addTrack(std::make_unique<SampleTrack>());
  song.setCurrentTrackId(other_track.getInternalId()); // deliberately not the Session View target

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);

  controller.sendCommand("toggle-record-arm");
  CHECK(controller.isThresholdArmed());
  CHECK(controller.isSessionRecording(track_id));
  CHECK(controller.getSessionRecordingClipIndex(track_id) == 0);
  CHECK(!controller.getPlaybackInfo().isPlaying()); // never auto-started

  controller.setRecordingTrackId(track_id);
  controller.startRecording();
  AudioBuffer block(1, 400);
  controller.addToSample(block);
  controller.beginSampleCapture(track_id);
  CHECK(controller.hasRecordingClip());
  controller.finishSampleCapture();

  auto & clips = song.getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(resolveInstanceAt(song, track_id, 0).clip_index == Arrangement::kNoInstance);

  controller.sendCommand("toggle-record-arm"); // disarm
  CHECK(!controller.isSessionRecording(track_id));
}

// Record Arm on a PercussionTrack's own clip in Session View is repurposed
// into "open this clip for editing on a Launchpad's step grid" (via
// setFocusedClip()/setDrumEditRequestListener()) instead of ever arming a
// take - a drum clip's own steps are never captured live. Reaches a
// lane-less PercussionTrack's own clip exactly the same way
// (toggle_record_arm_on_a_lane_less_percussion_clip_focuses_it_too below) -
// its step grid just shows empty until a lane exists.
TEST(toggle_record_arm_on_a_drum_machine_clip_focuses_it_instead_of_arming) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(std::make_unique<PercussionTrack>()));
  track.addLane(36); // step-sequenced - see this test's own header comment
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id));
  existing.setName("Beat 1");
  auto existing_id = existing.getId();

  int requested_track_id = -1;
  bool requested_opened = false;
  controller.setDrumEditRequestListener([&](int id, bool opened) { requested_track_id = id; requested_opened = opened; });

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0); // the occupied slot
  controller.sendCommand("toggle-record-arm");

  CHECK(!controller.isSessionRecording(track_id)); // never armed a take
  CHECK(!controller.isNoteCaptureArmed());
  CHECK(controller.getFocusedClipTrackId() == track_id);
  CHECK(controller.getFocusedClip() == existing_id); // the existing clip, not a new one
  CHECK(requested_track_id == track_id);
  CHECK(requested_opened);
  CHECK(song.getClips(track_id).size() == 1); // nothing created

  // Pressing Record Arm again on the same already-focused clip closes it
  // instead of doing nothing - the only way back to the track's own
  // background pattern (or to a different clip) via this same gesture.
  controller.sendCommand("toggle-record-arm");
  CHECK(controller.getFocusedClipTrackId() == -1);
  CHECK(controller.getFocusedClip().empty());
  CHECK(requested_track_id == track_id);
  CHECK(!requested_opened);
}

// A lane-less PercussionTrack is still a PercussionTrack -
// toggleDrumClipFocus() doesn't gate on lane count at all (its own doc
// comment), so Record Arm opens its own clip for step editing exactly the
// same way a step-sequenced one does; LaunchpadManager::refreshLeds() is
// what actually shows that empty (DeviceState::show_step_grid's own
// comment) - nothing here to distinguish at the Controller level.
TEST(toggle_record_arm_on_a_lane_less_percussion_clip_focuses_it_too) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<PercussionTrack>()); // no lanes
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id)); // an occupied slot, not an empty one
  auto existing_id = existing.getId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");

  CHECK(!controller.isTrackArmed(track_id)); // never armed a take
  CHECK(controller.getFocusedClipTrackId() == track_id);
  CHECK(controller.getFocusedClip() == existing_id);
}

// An empty slot lazily creates a fresh, looping clip rather than doing
// nothing or requiring a separate "new clip" gesture first.
TEST(toggle_record_arm_on_an_empty_drum_machine_slot_creates_a_clip) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{4, 4});

  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(std::make_unique<PercussionTrack>()));
  track.addLane(36); // step-sequenced, so Record Arm repurposes instead of arming
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0); // empty - no clips exist yet
  controller.sendCommand("toggle-record-arm");

  auto & clips = song.getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(clips[0].isLooping());
  // The song's own bar length, same as any other fresh clip - not clamped
  // to the connected Launchpad's own fixed 8-column grid, which pages
  // through a longer clip instead (LaunchpadManager's own
  // DeviceState::drum_edit_page).
  CHECK(clips[0].getLength() == 16);
  CHECK(controller.getFocusedClipTrackId() == track_id);
  CHECK(controller.getFocusedClip() == clips[0].getId());

  // Regression: a freshly-created, never-written-to clip's own read-only
  // auditioning (LaunchpadManager::triggerAuditionStep(), driven by
  // getFocusedClip() the instant it's set) used to crash - Clip::getLeafPattern()'s
  // const overload throws std::out_of_range on a Clip whose
  // patterns_by_track_ has never had an entry created for this track,
  // which a clip built only via addClip()+setters (no note ever written)
  // never gets.
  auto read_target = resolveReadTarget(song, track_id, 0, controller.getFocusedClip());
  CHECK(read_target.is_focused_override);
  CHECK(read_target.pattern != nullptr);
}

// Controller::toggleDrumClipFocus() called directly with a (track_id,
// clip_index) pair - the Launchpad's own CC91-held-as-shift gesture
// (LaunchpadManager::handleSessionPadEvent()) reaches it this way, never
// through clip_grid_focused_/setClipGridCursor() the way
// "toggle-record-arm" above does - same underlying open/close behavior,
// exercised through the other entry point.
TEST(toggle_drum_clip_focus_direct_call_opens_and_closes_a_clip) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(std::make_unique<PercussionTrack>()));
  track.addLane(36);
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id));
  existing.setName("Beat 1");
  auto existing_id = existing.getId();

  int requested_track_id = -1;
  bool requested_opened = false;
  controller.setDrumEditRequestListener([&](int id, bool opened) { requested_track_id = id; requested_opened = opened; });

  CHECK(controller.toggleDrumClipFocus(track_id, 0));
  CHECK(controller.getFocusedClipTrackId() == track_id);
  CHECK(controller.getFocusedClip() == existing_id);
  CHECK(requested_track_id == track_id);
  CHECK(requested_opened);

  // Same clip again - closes it, same as a second "toggle-record-arm"
  // press does.
  CHECK(controller.toggleDrumClipFocus(track_id, 0));
  CHECK(controller.getFocusedClipTrackId() == -1);
  CHECK(controller.getFocusedClip().empty());
  CHECK(!requested_opened);
}

// A no-op (false, nothing focused, no listener call) for anything that
// isn't a PercussionTrack's or a pitched InstrumentTrack's own clip at
// all (SampleTrack here - neither) - lets a caller like
// LaunchpadManager::handleSessionPadEvent() fall back to its own ordinary
// meaning for the gesture instead of silently swallowing the press.
// Neither a PercussionTrack nor an InstrumentTrack is ever declined this
// way (toggle_record_arm_on_a_lane_less_percussion_clip_focuses_it_too
// covers the lane-less-PercussionTrack case succeeding;
// toggle_drum_clip_focus_direct_call_opens_and_closes_a_clip below covers
// a step-sequenced one; the pitched case is exercised end to end by the
// step-sequencer's own e2e coverage, not a Controller-level test, since
// there's no model-level difference in how toggleDrumClipFocus() itself
// treats the two - see its own doc comment).
TEST(toggle_drum_clip_focus_is_a_no_op_off_a_non_percussion_clip) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & sample_track = song.addTrack(std::make_unique<SampleTrack>());
  auto sample_track_id = sample_track.getInternalId();
  song.addClip(Clip(sample_track_id));

  bool listener_called = false;
  controller.setDrumEditRequestListener([&](int, bool) { listener_called = true; });

  CHECK(!controller.toggleDrumClipFocus(sample_track_id, 0));
  CHECK(controller.getFocusedClipTrackId() == -1);
  CHECK(!listener_called);
}

// The pitched case toggleDrumClipFocus() now also accepts, directly (the
// Launchpad's own CC91-held-as-shift gesture reaches it this way, not
// through "toggle-record-arm" - see that command's own comment on why a
// pitched track is deliberately excluded there specifically).
TEST(toggle_drum_clip_focus_direct_call_opens_a_pitched_track_too) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & note_track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto note_track_id = note_track.getInternalId();
  auto & existing = song.addClip(Clip(note_track_id));
  auto existing_id = existing.getId();

  CHECK(controller.toggleDrumClipFocus(note_track_id, 0));
  CHECK(controller.getFocusedClipTrackId() == note_track_id);
  CHECK(controller.getFocusedClip() == existing_id);
}

// Controller::closeDrumClipFocus() - the Launchpad's own CC95 ("Session")
// press uses this to leave the sequencer outright, without already
// knowing which clip is open (unlike toggleDrumClipFocus(), which needs
// the exact (track_id, clip_index) that opened it).
TEST(close_drum_clip_focus_closes_whatever_is_open) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = dynamic_cast<PercussionTrack &>(song.addTrack(std::make_unique<PercussionTrack>()));
  track.addLane(36);
  auto track_id = track.getInternalId();
  auto & clip = song.addClip(Clip(track_id));
  clip.setName("Beat 1");

  int requested_track_id = -1;
  bool requested_opened = true;
  controller.setDrumEditRequestListener([&](int id, bool opened) { requested_track_id = id; requested_opened = opened; });

  controller.toggleDrumClipFocus(track_id, 0); // opens it
  CHECK(controller.getFocusedClipTrackId() == track_id);

  controller.closeDrumClipFocus();
  CHECK(controller.getFocusedClipTrackId() == -1);
  CHECK(controller.getFocusedClip().empty());
  CHECK(requested_track_id == track_id);
  CHECK(!requested_opened);
}

// A pure no-op when nothing is focused - no listener call, nothing to
// misfire on a CC95 press that was never showing a step grid to begin
// with.
TEST(close_drum_clip_focus_is_a_no_op_when_nothing_is_focused) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  bool listener_called = false;
  controller.setDrumEditRequestListener([&](int, bool) { listener_called = true; });

  controller.closeDrumClipFocus();
  CHECK(controller.getFocusedClipTrackId() == -1);
  CHECK(!listener_called);
}

// Any other track type keeps Record Arm's ordinary behavior even while
// Session View focused - the repurposing is drum-machine-only. Arming
// itself starts nothing - it only marks the track ready; a take begins
// once a pad on it is actually pressed (LaunchpadManager's own job).
TEST(toggle_record_arm_on_a_non_drum_machine_track_arms_normally) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");

  CHECK(controller.isTrackArmed(track_id));
  CHECK(!controller.isSessionRecording(track_id)); // arming alone starts no take
  // untouched - guards against the step sequencer's own pitched-track
  // support (toggleDrumClipFocus() now accepts an InstrumentTrack too)
  // silently preempting multi-track Record Arm here; the "toggle-record-
  // arm" command's own comment covers why it deliberately never calls
  // toggleDrumClipFocus() for anything but a PercussionTrack.
  CHECK(controller.getFocusedClipTrackId() == -1);
}

// ensureSessionRecordingClip()/extendSessionRecordingClipIfNeeded() are
// LaunchpadManager's own note-write path for a Session View take - driven
// directly by a caller-supplied absolute step (the free-running audition
// clock's own currentStep() in practice), never getPlaybackInfo(), and
// never placing an instance. absolute_step 0 here lands exactly on a bar
// boundary (rows_per_bar 4), so it becomes this take's own row 0 outright -
// previousBarRow()'s own snapping isn't separately exercised by this test.
TEST(ensure_session_recording_clip_creates_and_grows_at_the_exact_pressed_index) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 2); // an empty slot past the (currently empty) clip list
  controller.sendCommand("toggle-record-arm");
  CHECK(controller.isTrackArmed(track_id));
  CHECK(!controller.isSessionRecording(track_id)); // arming alone starts no take

  // A pad press on the armed track is what starts the take -
  // LaunchpadManager's own job in practice, done directly here to test
  // ensureSessionRecordingClip() in isolation.
  controller.armSessionTrackRecording(track_id, 2);
  CHECK(controller.isSessionRecording(track_id));

  controller.ensureSessionRecordingClip(track_id, 0);
  auto & clips = song.getClips(track_id);
  // Lands at exactly the pressed index (2), not the next unused one -
  // holes are allowed, so scene 2 can be recorded into even while scenes
  // 0/1 stay genuinely empty on this track.
  CHECK(clips.size() == 3);
  CHECK(clips[0].isEmpty()); // backfilled filler
  CHECK(clips[1].isEmpty()); // backfilled filler
  CHECK(controller.getSessionRecordingClipIndex(track_id) == 2);
  CHECK(clips[2].getLength() >= 1);

  clips[2].getLeafPattern().setNote(0, 0, Note(60, 100, 0));
  controller.extendSessionRecordingClipIfNeeded(track_id, 5);
  CHECK(clips[2].getLength() > 4); // grew ahead of row 5, same growth shape as extendRecordingClipsIfNeeded()

  CHECK(resolveInstanceAt(song, track_id, 0).clip_index == Arrangement::kNoInstance);
}

// Arming into a Session View slot that already holds a clip overwrites it
// in place - same id, content reset.
TEST(ensure_session_recording_clip_overwrites_an_occupied_slot_in_place) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id));
  existing.setName("Old take");
  existing.setLength(8);
  existing.getLeafPattern().setNote(0, 0, Note(40, 100, 0));
  auto existing_id = existing.getId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0); // the occupied slot
  controller.sendCommand("toggle-record-arm");
  controller.armSessionTrackRecording(track_id, 0);

  controller.ensureSessionRecordingClip(track_id, 0);
  auto & clips = song.getClips(track_id);
  CHECK(clips.size() == 1); // reused, not appended
  CHECK(clips[0].getId() == existing_id); // same clip identity preserved
  CHECK(clips[0].getName() == "Old take"); // name untouched by the reset
  CHECK(!clips[0].getLeafPattern().getNote(0, 0).isDefined()); // old content is gone
}

// A take's own row 0 is the *bar* the first note arrives in, not the exact
// step it lands on - a performer may deliberately skip the bar's first
// beat and start playing on a later one.
TEST(ensure_session_recording_clip_establishes_its_origin_from_the_containing_bar) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{4, 4}); // 4 beats * 4 rows/beat

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");
  controller.armSessionTrackRecording(track_id, 0);

  // First note lands on the bar's second beat (step 20 = bar 1's row 4),
  // not its first (step 16) - the origin still snaps back to the bar's own
  // start (16), so this note is recorded at relative row 4, not row 0.
  auto row = controller.ensureSessionRecordingClip(track_id, 20);
  CHECK(row == 4);

  // A later call within the same take keeps measuring from that same
  // established origin, not re-snapping to whatever bar the new step is in.
  row = controller.ensureSessionRecordingClip(track_id, 33);
  CHECK(row == 17);
}

// primeSessionRecordingOrigin() is the overdub signal: it fixes row 0
// outright *and* tells ensureSessionRecordingClip() this take is merging
// into an already-live clip rather than starting a fresh one, so its
// content is left completely undisturbed. ensureSessionRecordingClip()'s
// own first call must respect the primed origin rather than deriving a
// different one via previousBarRow(), which in general disagrees with an
// arbitrary primed step not itself a multiple of rows_per_bar from
// absolute row 0.
TEST(ensure_session_recording_clip_respects_a_primed_origin) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{4, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id));
  existing.setLength(32); // long enough that row 8 below never wraps
  existing.getLeafPattern().setNote(0, 0, Note(40, 100, 0));

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");
  controller.armSessionTrackRecording(track_id, 0);

  // 21 isn't a multiple of rows_per_bar (16) - previousBarRow(29, 16) would
  // otherwise snap to 16, not 21.
  controller.primeSessionRecordingOrigin(track_id, 21);
  auto row = controller.ensureSessionRecordingClip(track_id, 29);
  CHECK(row == 8);
  CHECK(existing.getLeafPattern().getNote(0, 0).isDefined()); // overdub - old content untouched
}

// extendSessionRecordingClipIfNeeded() grows a bar ahead of wherever the
// take currently is, regardless of how much of that actually ends up
// holding a note - trimSessionRecordingClip() (toggle-record-arm's own
// disarm path) is what cuts that growth back down to real content once a
// take actually ends, quantized up to the last written note's own
// containing bar rather than left at whatever the growth loop last reached.
TEST(trim_session_recording_clip_cuts_growth_back_to_the_last_written_bar) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");
  controller.armSessionTrackRecording(track_id, 0);

  auto row = controller.ensureSessionRecordingClip(track_id, 0);
  auto & clips = song.getClips(track_id);
  auto & clip = clips[static_cast<size_t>(controller.getSessionRecordingClipIndex(track_id))];
  clip.getLeafPattern().setNote(row, 0, Note(60, 100, 0));

  // The take then idles for several more bars (the performer stopped
  // playing but hasn't disarmed yet) - the clock keeps growing the clip
  // ahead of itself regardless.
  controller.extendSessionRecordingClipIfNeeded(track_id, 40);
  CHECK(clip.getLength() > 4); // grown well past the one real note

  controller.sendCommand("toggle-record-arm"); // disarm
  CHECK(clip.getLength() == 4); // trimmed to the note's own containing bar
}

// Disarming a take that never actually received a note (armed, then
// disarmed with nothing played) leaves a fresh one-bar clip rather than a
// zero-length one.
TEST(trim_session_recording_clip_with_no_notes_leaves_one_bar) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");
  controller.armSessionTrackRecording(track_id, 0);
  controller.ensureSessionRecordingClip(track_id, 0); // clip created, but no note ever written into it
  controller.sendCommand("toggle-record-arm"); // disarm

  auto & clips = song.getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(clips[0].getLength() == 4);
}

// A take is meant to be heard right back the instant it finishes -
// trimSessionRecordingClip() (toggle-record-arm's own disarm path) flips
// the clip to looping and hands its identity to
// takeCompletedSessionRecording() for LaunchpadManager to pick up, exactly
// once.
TEST(trim_session_recording_clip_loops_and_is_reported_exactly_once) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.setClipGridFocused(true);
  controller.setClipGridCursor(track_id, 0);
  controller.sendCommand("toggle-record-arm");
  controller.armSessionTrackRecording(track_id, 0);
  auto row = controller.ensureSessionRecordingClip(track_id, 0);
  auto & clips = song.getClips(track_id);
  clips[static_cast<size_t>(controller.getSessionRecordingClipIndex(track_id))].getLeafPattern().setNote(row, 0, Note(60, 100, 0));
  auto clip_index = controller.getSessionRecordingClipIndex(track_id);
  controller.sendCommand("toggle-record-arm"); // disarm

  CHECK(clips[static_cast<size_t>(clip_index)].isLooping());

  auto completed = controller.takeCompletedSessionRecording();
  CHECK(completed.has_value());
  if (completed) {
    CHECK(completed->track_id == track_id);
    CHECK(completed->clip_index == clip_index);
  }

  CHECK(!controller.takeCompletedSessionRecording().has_value()); // consumed, not re-reported
}

// armTrack()/disarmTrack()/toggleTrackArmed() are pure per-track bookkeeping,
// with no dependency on Session View focus or any particular track type -
// the hardware-agnostic foundation a future per-track physical arm button
// would call directly.
TEST(track_armed_state_is_independent_per_track) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track_a = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & track_b = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_a_id = track_a.getInternalId();
  auto track_b_id = track_b.getInternalId();

  CHECK(!controller.hasAnyTrackArmed());
  controller.armTrack(track_a_id);
  CHECK(controller.isTrackArmed(track_a_id));
  CHECK(!controller.isTrackArmed(track_b_id));
  CHECK(controller.hasAnyTrackArmed());

  controller.toggleTrackArmed(track_b_id); // arms it - not currently armed
  CHECK(controller.isTrackArmed(track_a_id));
  CHECK(controller.isTrackArmed(track_b_id));

  controller.disarmTrack(track_a_id);
  CHECK(!controller.isTrackArmed(track_a_id));
  CHECK(controller.isTrackArmed(track_b_id));
  CHECK(controller.hasAnyTrackArmed()); // track_b still is

  controller.toggleTrackArmed(track_b_id); // disarms it - currently armed
  CHECK(!controller.hasAnyTrackArmed());
}

// disarmTrack() stops an in-flight take immediately, not queued - the
// Controller-level guarantee LaunchpadManager's own quantized "press again"
// stop gesture builds on top of for the ordinary (queued) case, but a
// physical arm button going off has to take effect right away regardless.
TEST(disarm_track_stops_an_in_flight_take_immediately) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  controller.armTrack(track_id);
  controller.armSessionTrackRecording(track_id, 0);
  auto row = controller.ensureSessionRecordingClip(track_id, 0);
  auto & clips = song.getClips(track_id);
  clips[static_cast<size_t>(controller.getSessionRecordingClipIndex(track_id))].getLeafPattern().setNote(row, 0, Note(60, 100, 0));
  CHECK(controller.isSessionRecording(track_id));

  controller.disarmTrack(track_id);
  CHECK(!controller.isTrackArmed(track_id));
  CHECK(!controller.isSessionRecording(track_id)); // the take is gone, not just the arm state
  CHECK(clips[0].isLooping()); // trimmed and handed off, same as an explicit disarm via toggle-record-arm
  auto completed = controller.takeCompletedSessionRecording();
  CHECK(completed.has_value());
  if (completed) CHECK(completed->track_id == track_id);
}

// An overdub take's own row wraps around the target clip's fixed length
// instead of growing past it - playing longer than the clip just means
// further passes merge more notes into the same loop.
TEST(overdub_row_wraps_instead_of_growing_past_the_clip_length) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id));
  existing.setLength(4); // one bar - short enough to wrap well within this test
  existing.getLeafPattern().setNote(0, 0, Note(40, 100, 0));

  controller.armTrack(track_id);
  controller.armSessionTrackRecording(track_id, 0);
  controller.primeSessionRecordingOrigin(track_id, 0); // the overdub signal

  CHECK(controller.ensureSessionRecordingClip(track_id, 0) == 0);
  CHECK(controller.ensureSessionRecordingClip(track_id, 4) == 0); // one full loop later - wraps back to row 0
  CHECK(controller.ensureSessionRecordingClip(track_id, 9) == 1); // 9 - 0 = 9, 9 % 4 == 1

  // The clip's own length never grew to accommodate any of this.
  CHECK(existing.getLength() == 4);
  controller.extendSessionRecordingClipIfNeeded(track_id, 9);
  CHECK(existing.getLength() == 4); // still a no-op - nothing to grow for an overdub
}

// trimSessionRecordingClip() is a pure no-op for an overdub take - the
// clip was already correct (already playing, already the right length)
// throughout, so there's nothing to finalize and nothing new to report.
TEST(trim_session_recording_clip_is_a_no_op_for_an_overdub) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & existing = song.addClip(Clip(track_id));
  existing.setLength(4);
  existing.setLooping(true);
  existing.getLeafPattern().setNote(0, 0, Note(40, 100, 0));

  controller.armTrack(track_id);
  controller.armSessionTrackRecording(track_id, 0);
  controller.primeSessionRecordingOrigin(track_id, 0);
  controller.ensureSessionRecordingClip(track_id, 0);
  controller.ensureSessionRecordingClip(track_id, 1); // merges a second note in without disturbing the first
  existing.getLeafPattern().setNote(1, 0, Note(50, 100, 0));

  controller.trimSessionRecordingClip(track_id);
  CHECK(existing.getLength() == 4); // untouched
  CHECK(existing.isLooping()); // untouched (was already true)
  CHECK(existing.getLeafPattern().getNote(0, 0).isDefined()); // original content survives
  CHECK(existing.getLeafPattern().getNote(1, 0).isDefined()); // merged content survives
  CHECK(!controller.takeCompletedSessionRecording().has_value()); // nothing to hand off
}

// Several tracks can each have their own in-flight take at once - the
// per-track map, not a single shared slot.
TEST(several_tracks_can_record_concurrently) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{1, 4});

  auto & track_a = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & track_b = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_a_id = track_a.getInternalId();
  auto track_b_id = track_b.getInternalId();

  controller.armTrack(track_a_id);
  controller.armTrack(track_b_id);
  controller.armSessionTrackRecording(track_a_id, 0);
  controller.armSessionTrackRecording(track_b_id, 0);
  CHECK(controller.isSessionRecording(track_a_id));
  CHECK(controller.isSessionRecording(track_b_id));

  auto ids = controller.getSessionRecordingTrackIds();
  CHECK(ids.size() == 2);

  controller.ensureSessionRecordingClip(track_a_id, 0);
  controller.ensureSessionRecordingClip(track_b_id, 0);
  CHECK(controller.getSessionRecordingClipIndex(track_a_id) == 0);
  CHECK(controller.getSessionRecordingClipIndex(track_b_id) == 0);

  controller.trimSessionRecordingClip(track_a_id);
  CHECK(!controller.isSessionRecording(track_a_id));
  CHECK(controller.isSessionRecording(track_b_id)); // untouched by the other track's own finish
}

// End-to-end: sendCommand("merge-clip-to-background") resolves its target
// purely from Song::getCurrentTrackId() and the playhead
// (getPlaybackInfo()) - no PatternEditor/ArrangementGrid/ClipGrid
// involved at all, confirming the command really is reachable independent
// of any UI widget.
TEST(merge_clip_to_background_command_resolves_from_current_track_and_playhead) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(4);
  shot.setLooping(false);
  shot.getLeafPattern().setNote(0, 0, Note(60, 100));
  song.addClip(std::move(shot)); // index 0

  auto & arrangement = song.getArrangement();
  placeClipInstance(song, track_id, 5, 0); // covers rows 5-8

  song.setCurrentTrackId(track_id);
  controller.setEditPosition(5); // lands on the clip's first row

  controller.sendCommand("merge-clip-to-background");

  CHECK(resolveInstanceAt(song, track_id, 5).clip_index == Arrangement::kStopInstance);
  CHECK(arrangement.getNotes(5, track_id).size() == 1);
  CHECK(arrangement.getNotes(5, track_id)[0].getValue() == 60);
}

// No current track set (Song::getCurrentTrackId()'s own -1 default) -
// stays a silent no-op, same as ArrangementOps.h's own
// mergeClipToBackground() does for any other unresolvable case.
TEST(merge_clip_to_background_command_is_a_noop_with_no_current_track_set) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip shot(track_id);
  shot.setLength(4);
  shot.setLooping(false);
  song.addClip(std::move(shot)); // index 0

  placeClipInstance(song, track_id, 5, 0);

  CHECK(song.getCurrentTrackId() == -1); // never set
  controller.setEditPosition(5);

  controller.sendCommand("merge-clip-to-background");

  CHECK(resolveInstanceAt(song, track_id, 5).clip_index == 0); // still placed, untouched
}

// A live note-recording take writes into a real, individually-manageable
// Clip instance instead of falling through to the track's own background
// Pattern - ensureNoteRecordingClip()'s own core contract.
TEST(ensure_note_recording_clip_creates_and_places_a_real_clip_on_first_write) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);

  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(!clips.empty() && !clips[0].isLooping()); // non-looping by default - one specific take, not a pattern meant to auto-repeat
  CHECK(clip_ids.count(track_id) == 1);
  if (!clips.empty()) CHECK(clip_ids[track_id] == clips[0].getId());

  auto active = resolveInstanceAt(controller.getSong(), track_id, 0);
  CHECK(active.clip_index == 0);
}

// A live take's own first note rarely lands exactly on a bar boundary -
// the resulting clip is still placed at its own bar's start (previousBarRow()),
// matching every other real placement in the song being bar-quantized, not
// at the raw live row itself.
TEST(ensure_note_recording_clip_places_the_clip_at_the_start_of_its_own_bar) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{4, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 4); // row 4 - mid-bar, not the raw row the clip should land on

  auto active = resolveInstanceAt(controller.getSong(), track_id, 4);
  CHECK(active.clip_index == 0);
  CHECK(active.start_row == 0); // rounded back to bar 0's own start, not row 4
}

// Idempotent within the same session: a second write landing on a row the
// just-created clip already covers must not create a duplicate.
TEST(ensure_note_recording_clip_does_not_duplicate_at_the_same_row) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);

  CHECK(controller.getSong().getClips(track_id).size() == 1);
}

// A clip already active at the write position - placed before this
// session even started - is left alone; the write already lands somewhere
// real without any new clip.
TEST(ensure_note_recording_clip_leaves_an_already_active_clip_alone) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & song = controller.getSong();

  Clip pre_existing(track_id);
  pre_existing.setLooping(true);
  auto clip_index = 0;
  song.addClip(std::move(pre_existing));
  placeClipInstance(song, track_id, 0, clip_index);

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);

  CHECK(song.getClips(track_id).size() == 1); // no new clip
  CHECK(clip_ids.empty()); // never touched - resolveInstanceAt() already found something real
}

// Controller::getFocusedClip() already overrides resolution entirely
// (ArrangementOps.h's own resolveEditTarget()) - nothing to place.
TEST(ensure_note_recording_clip_does_nothing_while_a_clip_is_focused) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  controller.setFocusedClip(track_id, "some-clip-id");

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);

  CHECK(controller.getSong().getClips(track_id).empty());
}

// A long take's own clip keeps growing so resolveInstanceAt()'s one-shot
// expiry (non-looping by default) never silently drops it back to the
// background mid-take.
TEST(extend_recording_clips_if_needed_grows_the_clip_as_the_take_continues) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  CHECK(controller.getSong().getClips(track_id)[0].getLength() == 4); // a full bar right away, not left at 0 - see ensureNoteRecordingClip()'s own comment on why

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setAbsolutePos(0);
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });

  auto length_after_first_grow = controller.getSong().getClips(track_id)[0].getLength();
  CHECK(length_after_first_grow > 0);
  // Comfortably ahead now (at least a bar of headroom past row 0) - a
  // second call at the same row must not grow it again.
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });
  CHECK(controller.getSong().getClips(track_id)[0].getLength() == length_after_first_grow);

  // Advance close enough to the end of the current window to force
  // another grow.
  info.setAbsolutePos(length_after_first_grow - 1);
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });
  CHECK(controller.getSong().getClips(track_id)[0].getLength() > length_after_first_grow);
}

TEST(extend_recording_clips_if_needed_is_a_no_op_while_stopped_or_with_no_clips) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);

  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id }); // still stopped - PlaybackInfo defaults to not playing
  CHECK(controller.getSong().getClips(track_id)[0].getLength() == 16); // unchanged from its own creation-time length (a full bar of the default time signature)

  std::unordered_map<int, std::string> empty_clip_ids;
  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setAbsolutePos(0);
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(empty_clip_ids, { track_id }); // playing, but this caller has no clips of its own
  CHECK(controller.getSong().getClips(track_id)[0].getLength() == 16);
}

// A real bug report: Record Arm has no auto-stop-on-release the way
// PatternEditor's own keyboard session does, so a clip left ungated on
// "is a note actually held right now" would keep growing (and clearing
// everything in its own path via placeClipInstance()) for as long as the
// session merely stayed armed, long after the performer had released the
// note and stopped playing anything - eventually consuming the whole rest
// of the song.
TEST(extend_recording_clips_if_needed_does_not_grow_a_track_with_no_note_currently_held) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  auto initial_length = controller.getSong().getClips(track_id)[0].getLength();

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setAbsolutePos(initial_length - 1); // right at the edge of the clip's own current window
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, {}); // nothing currently held for this track

  CHECK(controller.getSong().getClips(track_id)[0].getLength() == initial_length);
}

// A real bug report: recording against playback the performer had already
// started manually (e.g. positioned at row 0 and pressed Space themselves,
// *before* arming/holding a note) never engages startAutoRecordSession()
// at all - isAutoRecording() stays false for the whole take, since this
// session never itself started the transport. A clip that already exists
// (this map's own entry) must still keep growing regardless - gating on
// isAutoRecording() would silently stop right after the clip's own initial one-bar length, real
// playback quietly outrunning it with nothing left to grow it further.
TEST(extend_recording_clips_if_needed_keeps_growing_even_when_this_session_never_started_the_transport) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  auto initial_length = controller.getSong().getClips(track_id)[0].getLength();

  PlaybackInfo info;
  info.setIsPlaying(true); // playback already running, e.g. started manually - not this session's own doing
  info.setAbsolutePos(initial_length - 1); // right at the edge of the clip's own current window
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });

  CHECK(controller.getSong().getClips(track_id)[0].getLength() > initial_length);
}

// A real bug report: a stray stop event sitting in a live take's own
// future path (leftover authoring, or an earlier interrupted take) froze
// growth the instant resolveInstanceAt() found it instead of this clip -
// the release's own note-off then landed in the background, and every
// later note started an entirely new clip, none of them ever recovering.
// A live take must overwrite whatever it grows across, the same
// "replaces, not merges with" rule ensureRowCleared() already gives the
// background Pattern.
TEST(extend_recording_clips_if_needed_overwrites_a_stop_it_grows_across) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & song = controller.getSong();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  auto initial_length = song.getClips(track_id)[0].getLength(); // one bar - see ensureNoteRecordingClip()'s own comment

  placeStopInstance(song, track_id, initial_length); // right where the clip's own window currently ends

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setAbsolutePos(initial_length - 1); // right at the edge - forces a grow
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });

  CHECK(song.getClips(track_id)[0].getLength() > initial_length);
  // The stop is gone, not merely outrun - resolveInstanceAt() at its own
  // former row now finds the recording clip itself, not kStopInstance.
  auto active = resolveInstanceAt(song, track_id, initial_length);
  CHECK(active.clip_index == 0);
}

// Same overwrite rule, but growing across a *different* real clip's own
// placement rather than a stop - the recording still wins.
TEST(extend_recording_clips_if_needed_overwrites_a_different_clip_it_grows_across) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setTimeSignature(TimeSignature{1, 4});

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & song = controller.getSong();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  auto initial_length = song.getClips(track_id)[0].getLength();
  auto recording_clip_id = clip_ids[track_id];

  Clip other(track_id);
  other.setLooping(true);
  song.addClip(std::move(other));
  auto other_index = static_cast<int>(song.getClips(track_id).size()) - 1;
  placeClipInstance(song, track_id, initial_length, other_index);
  CHECK(resolveInstanceAt(song, track_id, initial_length).clip_index == other_index);

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setAbsolutePos(initial_length - 1);
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });

  CHECK(song.getClips(track_id)[0].getLength() > initial_length);
  auto active = resolveInstanceAt(song, track_id, initial_length);
  CHECK(active.clip_index >= 0);
  CHECK(song.getClips(track_id)[static_cast<size_t>(active.clip_index)].getId() == recording_clip_id);
}

// applyNotePressure() had no test coverage at all before this - a live
// take's aftertouch arriving on a later row than the note-on it belongs
// to (the transport has moved on while the note is still held) writes a
// genuine aftertouch entry (Note::isAftertouch() - value stays -1,
// velocity becomes the pressure) at that row, into the track's own
// background Pattern here (no clip involved).
TEST(apply_note_pressure_writes_an_aftertouch_note) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  song.setCurrentTrackId(track_id);

  auto & arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(60, 100));

  controller.applyNotePressure(2, track_id, 0, 90, 0);

  auto & note = arrangement.getNote(2, track_id, 0);
  CHECK(note.isAftertouch());
  CHECK(note.getVelocity() == 90);
}

// The same, but through a real recording clip (ensureNoteRecordingClip())
// rather than the track's own background Pattern - applyNotePressure()
// resolves through resolveEditTarget() exactly like the note-on write
// itself did, so it lands in the clip's own leaf Pattern too, not the
// background underneath it.
TEST(apply_note_pressure_writes_aftertouch_into_a_recording_clip) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setTimeSignature(TimeSignature{4, 4});

  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  song.setCurrentTrackId(track_id);

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0);
  CHECK(!clip_ids.empty());

  auto edit_target = resolveEditTarget(song, track_id, 0, controller.getFocusedClip());
  edit_target.pattern->setNote(edit_target.effective_row, 0, Note(60, 100));

  controller.applyNotePressure(4, track_id, 0, 90, 0);

  auto read_target = resolveReadTarget(song, track_id, 4, controller.getFocusedClip());
  auto & note = read_target.pattern->getNote(read_target.effective_row, 0);
  CHECK(read_target.is_instance); // landed in the recording clip, not the background
  CHECK(note.isAftertouch());
  CHECK(note.getVelocity() == 90);
}

TEST(monitor_auto_hears_a_note_track_only_while_nothing_else_is_armed) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  auto & a = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(std::make_unique<InstrumentTrack>(0));
  song.setCurrentTrackId(a.getInternalId());

  // Nothing armed: live input is heard, today's behaviour.
  CHECK(controller.isMonitoring(a.getInternalId()));
  CHECK(controller.isMonitoring(b.getInternalId()));

  // Once a track is armed, only it is.
  controller.armTrack(b.getInternalId());
  CHECK(!controller.isMonitoring(a.getInternalId()));
  CHECK(controller.isMonitoring(b.getInternalId()));
  controller.disarmTrack(b.getInternalId());

  // Note capture arms the current track.
  controller.armNoteCapture();
  CHECK(controller.isMonitoring(a.getInternalId()));
  CHECK(!controller.isMonitoring(b.getInternalId()));
}

TEST(monitor_auto_hears_a_sample_track_only_while_armed) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  song.setCurrentTrackId(track.getInternalId());

  CHECK(!controller.isMonitoring(track.getInternalId()));
  controller.armThresholdRecording(track.getInternalId());
  CHECK(controller.isMonitoring(track.getInternalId()));
}

TEST(monitor_in_and_off_override_arming_and_cycle_in_order) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  auto & a = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & b = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto & leaf = dynamic_cast<LeafTrack &>(a);
  controller.armTrack(b.getInternalId());

  CHECK(leaf.getMonitor() == LeafTrack::Monitor::AUTO);
  controller.cycleTrackMonitor(a.getInternalId());
  CHECK(leaf.getMonitor() == LeafTrack::Monitor::IN);
  CHECK(controller.isMonitoring(a.getInternalId())); // not armed, heard anyway

  controller.cycleTrackMonitor(a.getInternalId());
  CHECK(leaf.getMonitor() == LeafTrack::Monitor::OFF);
  controller.armTrack(a.getInternalId());
  CHECK(!controller.isMonitoring(a.getInternalId())); // armed, never heard

  controller.cycleTrackMonitor(a.getInternalId());
  CHECK(leaf.getMonitor() == LeafTrack::Monitor::AUTO);
}

TEST(monitor_setting_round_trips_through_the_song_file) {
  namespace fs = std::filesystem;
  auto scratch_path = fs::path(TESTS_SCRATCH_DIR) / "controller_monitor_scratch.xml";
  fs::copy_file(fs::path(TESTS_FIXTURES_DIR) / "center_note.xml", scratch_path,
		fs::copy_options::overwrite_existing);

  ChannelConfiguration config(44100, 1);
  {
    Controller controller(config);
    CHECK(controller.openSong(scratch_path.string()));
    auto track_ids = controller.getSong().getPlayableTrackIds();
    CHECK(!track_ids.empty());
    controller.cycleTrackMonitor(track_ids.front()); // In
    CHECK(controller.sendCommand("save-song"));
  }
  CHECK(readFile(scratch_path.string()).find("monitor=\"in\"") != std::string::npos);

  Controller reopened(config);
  CHECK(reopened.openSong(scratch_path.string()));
  auto track_ids = reopened.getSong().getPlayableTrackIds();
  auto * leaf = dynamic_cast<LeafTrack *>(reopened.getSong().getMasterTrack().getChildByInternalId(track_ids.front()));
  CHECK(leaf && leaf->getMonitor() == LeafTrack::Monitor::IN);
  fs::remove(scratch_path);
}

TEST(sync_monitoring_sends_only_changes_for_monitoring_sample_tracks) {
  ChannelConfiguration config(8000, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  auto & sample = song.addTrack(std::make_unique<SampleTrack>());
  song.addTrack(std::make_unique<InstrumentTrack>(0)); // monitors notes, not audio - never sent
  auto & queue = controller.getPlaybackEventQueue();

  controller.syncMonitoring();
  CHECK(!queue.hasEvents()); // Auto, unarmed

  auto expect = [&](int on) {
    CHECK(queue.hasEvents());
    auto ev_ptr = queue.pop();
    auto ev = dynamic_cast<PlaybackControlEvent *>(ev_ptr.get());
    CHECK(ev && ev->getType() == PlaybackControlEvent::SET_TRACK_MONITORING);
    CHECK(ev && ev->getParameter1() == sample.getInternalId() && ev->getParameter2() == on);
    CHECK(!queue.hasEvents());
  };

  controller.armThresholdRecording(sample.getInternalId());
  controller.syncMonitoring();
  expect(1);
  controller.syncMonitoring();
  CHECK(!queue.hasEvents()); // unchanged - nothing resent

  controller.disarmThresholdRecording();
  controller.syncMonitoring();
  expect(0);
}

TEST(playback_info_reports_round_trip_latency_in_milliseconds) {
  PlaybackInfo info;
  info.setOutSampleRate(48000);

  // Nothing while capture isn't running - the info bar shows nothing.
  CHECK(info.getRoundTripLatencyFrames() == -1);
  CHECK(info.getRoundTripLatencyMs() == -1);

  info.setRoundTripLatency(1024, false); // 21.33ms
  CHECK(info.getRoundTripLatencyMs() == 21);
  CHECK(!info.isRoundTripLatencyNominal());

  info.setRoundTripLatency(1200, true); // 25ms exactly, unmeasured
  CHECK(info.getRoundTripLatencyMs() == 25);
  CHECK(info.isRoundTripLatencyNominal());

  // No rate yet (before the first snapshot) - nothing to report either.
  PlaybackInfo fresh;
  fresh.setRoundTripLatency(1024, false);
  CHECK(fresh.getRoundTripLatencyMs() == -1);
}
