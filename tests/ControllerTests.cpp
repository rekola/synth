#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/SampleContent.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/Clip.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/PlaybackInfo.h"
#include "../src/playback/PlaybackControlEvent.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

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
  info_a.setRowIdx(5);
  controller.setPlaybackInfo(info_a);

  auto buffer_b = controller.freshBufferName();
  controller.switchToBuffer(buffer_b);
  // A brand-new buffer must start out with fresh, default state, not
  // buffer_a's - not merely leftover live-scalar values.
  CHECK(controller.getRecordingTrackId() == 0);
  CHECK(controller.getPlaybackInfo().getRowIndex() == 0);

  controller.setRecordingTrackId(7);
  PlaybackInfo info_b;
  info_b.setRowIdx(9);
  controller.setPlaybackInfo(info_b);

  controller.switchToBuffer(buffer_a);
  CHECK(controller.getRecordingTrackId() == 3);
  CHECK(controller.getPlaybackInfo().getRowIndex() == 5);

  controller.switchToBuffer(buffer_b);
  CHECK(controller.getRecordingTrackId() == 7);
  CHECK(controller.getPlaybackInfo().getRowIndex() == 9);
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
  snapshot.setRowIdx(5);
  snapshot.setPositionEditSeq(1);
  controller.receivePlaybackSnapshot(buffer_b, snapshot);

  CHECK(controller.getPlaybackInfo().getRowIndex() == 5);
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

TEST(extend_recording_scene_grows_the_playing_scene_near_its_own_last_bar) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & song = controller.getSong();
  song.setRowsPerBar(1); // 1 bar = 1 row, for simple arithmetic below
  auto & scene = song.getOrCreateScene(0);
  scene.setLengthBars(3); // rows 0-2

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(0); // well inside the scene - not near the end yet
  controller.setPlaybackInfo(info);
  controller.extendRecordingSceneIfNeeded(true);
  CHECK(scene.getLengthBars() == 3); // untouched

  info.setRowIdx(2); // the scene's own last row
  controller.setPlaybackInfo(info);
  controller.extendRecordingSceneIfNeeded(true);
  CHECK(scene.getLengthBars() == 4); // grew by one more bar

  // Not recording: no-op even at the very end.
  info.setRowIdx(3);
  controller.setPlaybackInfo(info);
  controller.extendRecordingSceneIfNeeded(false);
  CHECK(scene.getLengthBars() == 4);

  // Stopped: no-op even while "recording".
  info.setIsPlaying(false);
  controller.setPlaybackInfo(info);
  controller.extendRecordingSceneIfNeeded(true);
  CHECK(scene.getLengthBars() == 4);
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
  // Not a queue.hasEvents() check here first - EventQueue only updates
  // its own pending count inside pop()'s own read(), so hasEvents() reads
  // as false for anything pushed before the first pop() ever happens;
  // pop() itself is what's actually safe to call directly (the write
  // already landed on the underlying socket, so it returns immediately
  // rather than genuinely blocking). Held in its own unique_ptr, not
  // chained straight into dynamic_cast(...pop().get()) - pop()'s own
  // return value is a temporary that would otherwise be destroyed (along
  // with the Event it owns) at the end of that one statement, leaving the
  // raw pointer dangling for every access after it.
  auto ev1_ptr = queue.pop();
  auto ev1 = dynamic_cast<PlaybackControlEvent *>(ev1_ptr.get());
  CHECK(ev1 != nullptr);
  CHECK(ev1->getType() == PlaybackControlEvent::STOP_ALL_NOTES);
  CHECK(ev1->getParameter1() == 3); // the *previous* focus's own track, not the new one
  CHECK(!queue.hasEvents()); // exactly one event, no more - now reliable, after the pop() above
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

// toggleFocusedClip() - SessionView's own Enter primitive: re-pressing
// Enter on the already-focused clip clears it (same silence-on-change
// behavior above); pressing it on a different clip switches to that one.
TEST(toggle_focused_clip_clears_when_already_focused_else_switches) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto & queue = controller.getPlaybackEventQueue();

  controller.toggleFocusedClip(2, "verse"); // nothing focused yet - sets it
  CHECK(controller.getFocusedClip() == "verse");
  CHECK(!queue.hasEvents()); // nothing to silence on the very first focus

  controller.toggleFocusedClip(2, "verse"); // same clip again - clears it
  auto ev_ptr = queue.pop();
  auto ev = dynamic_cast<PlaybackControlEvent *>(ev_ptr.get());
  CHECK(ev != nullptr);
  CHECK(ev->getType() == PlaybackControlEvent::STOP_ALL_NOTES);
  CHECK(ev->getParameter1() == 2);
  CHECK(controller.getFocusedClip().empty());
  CHECK(controller.getFocusedClipTrackId() == -1);
}

// Song::getCurrentTrackId() lives on the Song itself, not in any of
// Controller's per-buffer-mirrored scalars - the PatternEditor and
// SessionView aspects of one song (canonicalBufferName()'s own pair)
// automatically see the identical value with no separate save/load
// bookkeeping, unlike getPlaybackInfo() and friends.
TEST(current_track_id_is_shared_across_a_songs_own_buffer_aspects) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto name = controller.freshBufferName();
  controller.switchToBuffer(name);
  controller.getSong().setCurrentTrackId(7);

  auto alias = controller.openSessionViewBuffer();
  CHECK(controller.getSong().getCurrentTrackId() == 7); // same Song, same value

  controller.getSong().setCurrentTrackId(9);
  controller.switchToBuffer(name); // back to the PatternEditor aspect
  CHECK(controller.getSong().getCurrentTrackId() == 9); // set through the other aspect, still visible here
}

// PatternEditor and SessionView are symmetric buffer-list aspects of the
// same song now - either can be opened or closed independently, and
// closing one never closes the underlying song as long as the other (or
// some other song's own buffer) stays open.
TEST(session_view_buffer_can_close_without_closing_the_song) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto name = controller.freshBufferName();
  controller.switchToBuffer(name);

  auto alias = controller.openSessionViewBuffer();
  CHECK(alias == name + " [Session]");
  CHECK(controller.isSessionViewBuffer(alias));
  CHECK(controller.getSelectedBufferName() == alias);
  CHECK(controller.getActiveBufferName() == name); // resolves to the real song either way

  // Closing the SessionView aspect (the only thing selected right now)
  // returns to the PatternEditor aspect - the song itself is untouched.
  CHECK(controller.killActiveBuffer());
  CHECK(controller.getSelectedBufferName() == name);
  CHECK(!controller.isSessionViewBuffer(controller.getSelectedBufferName()));
  auto names = controller.getBufferNames();
  CHECK(std::find(names.begin(), names.end(), alias) == names.end()); // the SessionView entry is gone
  CHECK(std::find(names.begin(), names.end(), name) != names.end()); // the song's own PatternEditor entry remains
}

// The new capability this session's own request was actually about:
// PatternEditor's own buffer-list entry is no longer privileged - closing
// it while SessionView stays open must leave the song alive under
// SessionView, not destroy it the way closing the sole "canonical" entry
// used to.
TEST(pattern_editor_buffer_can_close_while_session_view_stays_open) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto name = controller.freshBufferName();
  controller.switchToBuffer(name); // PatternEditor aspect, the only one so far
  controller.openSessionViewBuffer(); // now both aspects are open

  // Make the PatternEditor aspect the active one, then close it.
  controller.switchToBuffer(name);
  CHECK(controller.activeSongHasOtherOpenViews()); // SessionView is still open on this song
  CHECK(controller.killActiveBuffer());

  // The song survives under its SessionView aspect - not gone, not
  // silently recreated as a fresh empty buffer under the old name.
  CHECK(controller.getSelectedBufferName() == name + " [Session]");
  CHECK(controller.isSessionViewBuffer(controller.getSelectedBufferName()));
  CHECK(controller.getActiveBufferName() == name);
  auto names = controller.getBufferNames();
  CHECK(std::find(names.begin(), names.end(), name) == names.end()); // the PatternEditor entry is gone
  CHECK(std::find(names.begin(), names.end(), name + " [Session]") != names.end());

  // And it can be reopened later, landing back on the very same song.
  auto reopened = controller.openPatternEditorBuffer();
  CHECK(reopened == name);
  CHECK(controller.getSelectedBufferName() == name);
  CHECK(controller.getActiveBufferName() == name);
}

// Closing a song's *last* remaining view (regardless of which aspect it
// is) closes the underlying song itself - the same "always keep at least
// one buffer open" guarantee the old canonical-buffer-only design had,
// now checked across every open view of every song rather than just
// songs_' own count.
TEST(closing_the_last_view_of_a_song_closes_the_song_itself) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto other = controller.freshBufferName();
  controller.switchToBuffer(other); // a second song, so the one under test isn't the app's only buffer

  auto name = controller.freshBufferName();
  controller.switchToBuffer(name); // only the PatternEditor aspect is open on this one
  CHECK(!controller.activeSongHasOtherOpenViews());
  CHECK(controller.killActiveBuffer());

  auto names = controller.getBufferNames();
  CHECK(std::find(names.begin(), names.end(), name) == names.end());
  CHECK(std::find(names.begin(), names.end(), name + " [Session]") == names.end());
  CHECK(controller.getSelectedBufferName() == other); // switched to the only buffer left
}

// killActiveBuffer() still refuses when it's genuinely the only buffer-
// list entry open anywhere, whichever aspect it happens to be.
TEST(kill_active_buffer_refuses_the_only_open_view) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto name = controller.freshBufferName();
  controller.switchToBuffer(name);
  CHECK(controller.getBufferNames().size() == 1);
  CHECK(!controller.killActiveBuffer());
  CHECK(controller.getSelectedBufferName() == name); // untouched
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
    auto * content = clips[0].getSampleContent();
    CHECK(content != nullptr);
    if (content && content->getBuffer()) {
      // Both blocks - the same shared_ptr addToSample() appends into.
      CHECK(content->getBuffer()->numberOfFrames() == 800);
    }
    if (content) CHECK(content->getOriginalTempo() == 120);
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
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();
  controller.setRecordingTrackId(track_id);
  controller.startRecording();

  controller.armRecordingStart(0, 2); // scene 0, row 2 - the take's own real start
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
    auto * content = clips[0].getSampleContent();
    CHECK(content != nullptr);
    if (content) CHECK_NEAR(content->getInPoint(), 100.0f / 8000.0f, 1e-6f);
  }

  // Placed at the snapshotted row (2), not row 0 - resolveInstanceAt()
  // only finds it active there.
  auto & scene = controller.getSong().getScene(0);
  auto active_at_start = resolveInstanceAt(controller.getSong(), scene, track_id, 2);
  CHECK(active_at_start.clip_index == 0);
  auto active_at_zero = resolveInstanceAt(controller.getSong(), scene, track_id, 0);
  CHECK(active_at_zero.clip_index == Scene::kNoInstance);

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

// A real bug report: a captured take defaulted to Clip's own looping=true,
// so a placed instance reached through the rest of the scene and kept
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
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  // An old stop marker, well ahead of where the new take starts - left
  // over from some earlier, unrelated arrangement edit.
  auto & scene = controller.getSong().getScene(0);
  placeStopInstance(scene, track_id, 20);

  controller.setRecordingTrackId(track_id);
  controller.startRecording();
  controller.armRecordingStart(0, 0);

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
  CHECK(resolveInstanceAt(controller.getSong(), scene, track_id, 20).clip_index == Scene::kStopInstance);

  // The transport advances well past the take's own current (small)
  // reach, the same way real per-row playback would while recording
  // continues.
  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(20);
  controller.setPlaybackInfo(info);
  controller.extendRecordingSampleClipIfNeeded();

  CHECK(clips[0].getLength() > 4); // grew to keep ahead of the current row
  // The stale stop marker is gone, swept by the same placeClipInstance()
  // re-run extendRecordingClipsIfNeeded() already uses for note takes -
  // row 20 now resolves to the still-active recording clip instead.
  CHECK(resolveInstanceAt(controller.getSong(), scene, track_id, 20).clip_index == 0);
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

  auto & scene = controller.getSong().addScene();
  auto active = resolveInstanceAt(controller.getSong(), scene, track_id, 0);
  CHECK(active.clip_index == Scene::kNoInstance); // never placed anywhere

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

// End-to-end: sendCommand("merge-clip-to-background") resolves its target
// purely from Song::getCurrentTrackId() and the playhead
// (getPlaybackInfo()) - no PatternEditor/ArrangementGrid/SessionView
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

  // switchToBuffer() already seeded scene 0 for a fresh buffer - reuse it
  // rather than addScene()'ing a second one, so the playhead (which
  // normalizes against scene 0 first) actually lands where the clip is.
  auto & scene = song.getScene(0);
  placeClipInstance(song, scene, track_id, 5, 0); // covers rows 5-8

  song.setCurrentTrackId(track_id);
  controller.setEditPosition(5); // lands inside scene 0's own row 5

  controller.sendCommand("merge-clip-to-background");

  CHECK(resolveInstanceAt(song, scene, track_id, 5).clip_index == Scene::kStopInstance);
  CHECK(scene.getNotes(5, track_id).size() == 1);
  CHECK(scene.getNotes(5, track_id)[0].getValue() == 60);
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

  auto & scene = song.getScene(0); // switchToBuffer() already seeded this one - see the other test's own comment
  placeClipInstance(song, scene, track_id, 5, 0);

  CHECK(song.getCurrentTrackId() == -1); // never set
  controller.setEditPosition(5);

  controller.sendCommand("merge-clip-to-background");

  CHECK(resolveInstanceAt(song, scene, track_id, 5).clip_index == 0); // still placed, untouched
}

// A live note-recording take writes into a real, individually-manageable
// Clip instance instead of falling through to the scene's own background
// Pattern - ensureNoteRecordingClip()'s own core contract.
TEST(ensure_note_recording_clip_creates_and_places_a_real_clip_on_first_write) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);

  auto & clips = controller.getSong().getClips(track_id);
  CHECK(clips.size() == 1);
  CHECK(!clips.empty() && !clips[0].isLooping()); // non-looping by default - one specific take, not a pattern meant to auto-repeat
  CHECK(clip_ids.count(track_id) == 1);
  if (!clips.empty()) CHECK(clip_ids[track_id] == clips[0].getId());

  auto & scene = controller.getSong().getScene(0);
  auto active = resolveInstanceAt(controller.getSong(), scene, track_id, 0);
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
  controller.getSong().setRowsPerBar(16);

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 4); // row 4 - mid-bar, not the raw row the clip should land on

  auto & scene = controller.getSong().getScene(0);
  auto active = resolveInstanceAt(controller.getSong(), scene, track_id, 4);
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
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);

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
  auto & scene = song.getScene(0); // switchToBuffer(freshBufferName()) already seeded scene 0 - the same one ensureNoteRecordingClip() below resolves against

  Clip pre_existing(track_id);
  pre_existing.setLooping(true);
  auto clip_index = 0;
  song.addClip(std::move(pre_existing));
  placeClipInstance(song, scene, track_id, 0, clip_index);

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);

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
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);

  CHECK(controller.getSong().getClips(track_id).empty());
}

// Clip::setLength()'s own counterpart to extendRecordingSceneIfNeeded() -
// a long take's own clip keeps growing so resolveInstanceAt()'s one-shot
// expiry (non-looping by default) never silently drops it back to the
// background mid-take.
TEST(extend_recording_clips_if_needed_grows_the_clip_as_the_take_continues) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);
  CHECK(controller.getSong().getClips(track_id)[0].getLength() == 4); // a full bar right away, not left at 0 - see ensureNoteRecordingClip()'s own comment on why

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(0);
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
  info.setRowIdx(length_after_first_grow - 1);
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
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);

  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id }); // still stopped - PlaybackInfo defaults to not playing
  CHECK(controller.getSong().getClips(track_id)[0].getLength() == 16); // unchanged from its own creation-time length (a full bar, default rowsPerBar)

  std::unordered_map<int, std::string> empty_clip_ids;
  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(0);
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
// of the scene.
TEST(extend_recording_clips_if_needed_does_not_grow_a_track_with_no_note_currently_held) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);
  auto initial_length = controller.getSong().getClips(track_id)[0].getLength();

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(initial_length - 1); // right at the edge of the clip's own current window
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
// isAutoRecording() the way extendRecordingSceneIfNeeded() does would
// silently stop right after the clip's own initial one-bar length, real
// playback quietly outrunning it with nothing left to grow it further.
TEST(extend_recording_clips_if_needed_keeps_growing_even_when_this_session_never_started_the_transport) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);
  auto initial_length = controller.getSong().getClips(track_id)[0].getLength();

  PlaybackInfo info;
  info.setIsPlaying(true); // playback already running, e.g. started manually - not this session's own doing
  info.setPatternIdx(0);
  info.setRowIdx(initial_length - 1); // right at the edge of the clip's own current window
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
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & song = controller.getSong();
  auto & scene = song.getScene(0);

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);
  auto initial_length = song.getClips(track_id)[0].getLength(); // one bar - see ensureNoteRecordingClip()'s own comment

  placeStopInstance(scene, track_id, initial_length); // right where the clip's own window currently ends

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(initial_length - 1); // right at the edge - forces a grow
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });

  CHECK(song.getClips(track_id)[0].getLength() > initial_length);
  // The stop is gone, not merely outrun - resolveInstanceAt() at its own
  // former row now finds the recording clip itself, not kStopInstance.
  auto active = resolveInstanceAt(song, scene, track_id, initial_length);
  CHECK(active.clip_index == 0);
}

// Same overwrite rule, but growing across a *different* real clip's own
// placement rather than a stop - the recording still wins.
TEST(extend_recording_clips_if_needed_overwrites_a_different_clip_it_grows_across) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.getSong().setRowsPerBar(4);

  auto & track = controller.getSong().addTrack(std::make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();
  auto & song = controller.getSong();
  auto & scene = song.getScene(0);

  std::unordered_map<int, std::string> clip_ids;
  controller.ensureNoteRecordingClip(clip_ids, track_id, 0, 0);
  auto initial_length = song.getClips(track_id)[0].getLength();
  auto recording_clip_id = clip_ids[track_id];

  Clip other(track_id);
  other.setLooping(true);
  song.addClip(std::move(other));
  auto other_index = static_cast<int>(song.getClips(track_id).size()) - 1;
  placeClipInstance(song, scene, track_id, initial_length, other_index);
  CHECK(resolveInstanceAt(song, scene, track_id, initial_length).clip_index == other_index);

  PlaybackInfo info;
  info.setIsPlaying(true);
  info.setPatternIdx(0);
  info.setRowIdx(initial_length - 1);
  controller.setPlaybackInfo(info);
  controller.extendRecordingClipsIfNeeded(clip_ids, { track_id });

  CHECK(song.getClips(track_id)[0].getLength() > initial_length);
  auto active = resolveInstanceAt(song, scene, track_id, initial_length);
  CHECK(active.clip_index >= 0);
  CHECK(song.getClips(track_id)[static_cast<size_t>(active.clip_index)].getId() == recording_clip_id);
}
