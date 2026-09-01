#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/model/Song.h"
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
