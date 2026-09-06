
#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "audio/AudioBuffer.h"
#include "model/Version.h"
#include "instruments/InstrumentProvider.h"
#include "playback/EventQueue.h"
#include "state/PlaybackInfo.h"
#include "ambisonic/ChannelConfiguration.h"
#include "ambisonic/MixerType.h"
#include "bus/BusEffectRegistry.h"
#include "ui/CommandRegistry.h"
#include "util/constants.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Song;

class Controller {
 public:
  Controller(ChannelConfiguration _channel_config);

  // Player::play() (the audio thread) needs this: openSong()/saveSongAs()/
  // switchToBuffer()/killActiveBuffer() all reassign active_buffer_name_
  // (and songs_ itself) on the UI thread, and a plain `Song &` from
  // *before* such a reassignment is a dangling reference the instant the
  // old Song's refcount (an entry in songs_ was
  // its only owner) drops to 0, which a UI-thread reassignment can do
  // synchronously, regardless of when the audio thread later notices it.
  // Returning an actual shared_ptr copy - under the same mutex every one of
  // those methods takes, see song_mutex_'s own comment - keeps the *old*
  // Song object alive for as long as the audio thread's own copy of the
  // pointer is still in use, however long after the UI thread has moved on
  // to a new one.
  std::shared_ptr<Song> getCurrentSong() const {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto it = songs_.find(canonicalBufferName(active_buffer_name_));
    return it == songs_.end() ? nullptr : it->second;
  }

  // getCurrentSong()'s own parameterized sibling - Player now keeps a live
  // SongState per buffer that's actually made sound (see the per-buffer
  // editing/playback-state plan's Part B), not just for whichever buffer
  // is active, so it needs to resolve an arbitrary buffer's own Song by
  // name (from PlaybackControlEvent::getBufferName()), not only "the
  // current" one. nullptr if `name` isn't (or is no longer) open - a
  // buffer can be killed on the UI thread between this event being pushed
  // and the audio thread actually processing it.
  std::shared_ptr<Song> getSongByName(const std::string & name) const {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto it = songs_.find(canonicalBufferName(name));
    return it == songs_.end() ? nullptr : it->second;
  }

  // UI-thread convenience wrappers around getCurrentSong() - see that
  // method's own comment for why the audio thread must go through the
  // locked shared_ptr instead. Never concurrent with a reassignment
  // (also always on the UI thread), so no dangling-reference risk here.
  Song & getSong() { return *getCurrentSong(); }
  const Song & getSong() const { return *getCurrentSong(); }

  // The active *Song*'s own real id - always canonical, never a
  // SessionView-aspect name (see canonicalBufferName()'s own comment),
  // which is what every caller that actually means "the real underlying
  // Song" wants: file paths, PlaybackControlEvent tagging, PatternEditor's
  // own per-buffer state key. By value now (not a reference) since a
  // SessionView-aspect active_buffer_name_ resolves to a *different*
  // string, not a sub-object of it. UI-thread-only, like song_mutex_'s
  // other UI-thread-only reads, so no lock needed here (see song_mutex_'s
  // own comment). See getSelectedBufferName() below for the one thing
  // that still wants the raw, aspect-suffixed name.
  std::string getActiveBufferName() const { return canonicalBufferName(active_buffer_name_); }

  // The literal buffer-list entry currently selected - unlike
  // getActiveBufferName() above, this can be a SessionView aspect's own
  // name. Exists for the two places that must show/compare the exact
  // selected row rather than the Song it resolves to: the Buffers menu's
  // own "current" marker, and UI's own buffer-change listener (to tell
  // which aspect the newly-active entry is at all).
  const std::string & getSelectedBufferName() const { return active_buffer_name_; }

  // Thread-safe counterpart to getActiveBufferName() above (always
  // canonical, same reasoning) - Player.cpp's audio thread needs to know
  // which buffer is active too now (to decide which live buffer's own
  // AuxA/AuxB send-bus meters to report for the volume meter - see
  // AudioBlockEvent.h), and a bare read of active_buffer_name_ there
  // would race a UI-thread switchToBuffer()/killActiveBuffer()/
  // renameActiveBuffer() reassignment the same way getCurrentSong() would
  // without going through song_mutex_.
  std::string getActiveBufferNameThreadSafe() const {
    std::lock_guard<std::mutex> guard(song_mutex_);
    return canonicalBufferName(active_buffer_name_);
  }

  // Every open buffer-list entry's name, in name-sorted order - every
  // open aspect (PatternEditor and/or SessionView, see BufferAspect's own
  // comment) of every open song. The Buffers menu (TerminalMenu::
  // refreshBuffers()), select-named-buffer's own completion, and any
  // future buffer-listing command read this rather than songs_ directly.
  std::vector<std::string> getBufferNames() const {
    std::set<std::string> names;
    for (auto & [song_id, aspects] : open_aspects_by_song_) {
      for (auto aspect : aspects) names.insert(bufferNameFor(song_id, aspect));
    }
    return std::vector<std::string>(names.begin(), names.end());
  }

  // Whether `name`'s own buffer-list entry is a SessionView aspect (see
  // BufferAspect's own comment), a pure formatting check (does it end in
  // the " [Session]" suffix) rather than a lookup - correct for any name
  // that's actually open (every caller's own case), regardless of which
  // aspect happens to be open for that particular song right now. UI's
  // own buffer-change listener uses this to decide whether the
  // newly-selected buffer should show SessionView in place of
  // PatternEditor.
  bool isSessionViewBuffer(const std::string & name) const { return aspectFor(name) == BufferAspect::SESSION_VIEW; }

  // Switches to (opening the first time - idempotent after that) the
  // SessionView/PatternEditor aspect of the currently active song: a
  // second buffer-list entry (song id, or song id + " [Session]") that
  // resolves to the exact same Song, live playback/edit state, and
  // save-dirty tracking as whichever aspect is currently active (see
  // canonicalBufferName()'s own comment) - the two differ only in which
  // UI aspect is shown for them, and either can be closed independently
  // without closing the underlying song as long as the other (or the song
  // itself, if neither is currently open - see killActiveBuffer()) stays
  // open. Calling either while already viewing that same aspect is a
  // harmless no-op. Returns the buffer's own name (for a caller that
  // wants to e.g. show it in a status message).
  std::string openSessionViewBuffer();
  std::string openPatternEditorBuffer();

  // Whether killing the active buffer right now would only close *this*
  // aspect-view (the underlying song stays open under its other one) or
  // would close the song itself (its only remaining view) - UI's own
  // kill-buffer command uses this to skip the "discard unsaved changes?"
  // prompt when nothing would actually be discarded.
  bool activeSongHasOtherOpenViews() const {
    auto it = open_aspects_by_song_.find(canonicalBufferName(active_buffer_name_));
    return it != open_aspects_by_song_.end() && it->second.size() > 1;
  }

  // Emacs-style uniquify: `name`'s own basename, unless another open
  // buffer shares it, in which case just enough of the parent directory
  // is appended (in "<dir>" / "<dir2/dir1>" ... form, growing until
  // unique) to tell them apart - the display text both the Buffers menu
  // (TerminalMenu::rebuild()) and the status bar (InfoLine) show, so a
  // buffer never looks identical to some other open one in either place.
  // A pure function of the current songs_ key set, not anything about
  // `name`'s own Song content - see the free uniqueDisplayName() helper
  // on Controller.cpp for the actual algorithm.
  std::string getBufferDisplayName(const std::string & name) const;

  // Song::getVersion() (incVersion() for structural changes,
  // incMinorVersion() for note/command content edits - see Song.h/
  // PatternEditor.cpp's own call sites) against a baseline snapshotted
  // whenever the *active* buffer was last freshly created, opened, saved,
  // or switched to. Not a precise "dirty" bit (a mutation path that
  // forgets to call either would go unnoticed), but reuses an existing,
  // already-pervasive mechanism rather than adding a parallel one - good
  // enough to gate kill-buffer's "discard unsaved changes?"
  // prompt (UI.cpp). See hasAnyUnsavedChanges() below for the all-buffers
  // counterpart save-buffers-kill-terminal needs instead.
  bool hasUnsavedChanges() const;
  // Every open buffer checked the same way hasUnsavedChanges() checks just
  // the active one - save-buffers-kill-terminal's own "N modified buffers -
  // quit anyway?" gate, so quitting can never silently drop a buffer the
  // user never even switched to (and so never saw hasUnsavedChanges()
  // warn about).
  bool hasAnyUnsavedChanges() const;

  // Opens `filename` as a new buffer, or - if it's already open - just
  // switches to that existing buffer instead of re-reading it from disk
  // (which would silently discard any in-memory edits the open copy has
  // that the file itself doesn't).
  bool openSong(const std::string & filename);
  // "Save As": like sendCommand("save-song") (saves the current song, and
  // resets hasUnsavedChanges()'s baseline the same way), but to `filename`
  // rather than whatever filename the song was opened/created with - and,
  // standard Save As semantics, subsequent plain saves target `filename`
  // too from now on (active_buffer_name_/songs_'s key are both renamed,
  // matching what it already means for openSong()).
  void saveSongAs(const std::string & filename);

  // Makes buffer `name` active - creating it fresh (an empty song, same
  // starter content the old createNewSong() used to set up) first if it
  // isn't already open, exactly like Emacs's own switch-to-buffer does
  // for an unrecognized name. There's no separate "New" command any more
  // (Emacs doesn't have one either) - main.cpp's own no-file-given
  // startup path is just switchToBuffer(freshBufferName()) below, and
  // select-named-buffer (UI.cpp) covers it interactively: typing a name
  // nothing has open yet creates it. The shared tail of cycleBuffer()
  // below too.
  void switchToBuffer(const std::string & name);
  // Switches to the buffer immediately after (forward) or before
  // (!forward) the active one in songs_'s own (name-sorted, i.e. map
  // iteration) order, wrapping around at either end - next-buffer/
  // previous-buffer's shared logic. A no-op with only one buffer open.
  void cycleBuffer(bool forward);
  // The buffer name cycleBuffer(true) would switch to from here, without
  // actually switching - select-named-buffer's (UI.cpp) own "default" for
  // Emacs's own "Switch to buffer (default ...): " prompt convention.
  // Real Emacs defaults to the most recently *other* selected buffer;
  // without any MRU tracking here, the next one in songs_'s own order is
  // the closest equivalent, and with only one buffer open there's no
  // "other" to default to at all - "" then, so the prompt falls back to a
  // plain "Switch to buffer: " with nothing to default an empty answer to.
  std::string getDefaultSwitchTarget() const;
  // A buffer name switchToBuffer() is guaranteed not to collide with one
  // already open - "song.xml" the first time, "song-2.xml"/"song-3.xml"/
  // ... after. main.cpp's own startup use only ever needs this once (there's
  // nothing open yet to collide with), but it's still not safe to just
  // hardcode "song.xml" there: switchToBuffer() treats an already-open
  // name as "switch to it", so a hardcoded name would risk silently
  // reattaching to some other buffer that happens to already be called
  // that instead of creating a genuinely new one.
  std::string freshBufferName() const;
  // Closes the active buffer and switches to another open one (name-
  // sorted first remaining) - false, refusing, if it's the only buffer
  // open, since the editor always needs at least one song. Never checks
  // hasUnsavedChanges() itself - same "logic here, confirmation in UI"
  // split openSong()'s own discard-free design already follows;
  // kill-buffer (UI.cpp) is the one that prompts first.
  bool killActiveBuffer();

  bool sendCommand(std::string_view s);

  // Lets the UI layer (which Controller, part of the headless-testable
  // synth_engine lib, must not depend on) supply a fallback for command
  // names sendCommand() doesn't recognize itself - e.g. per-widget Emacs
  // commands like "set-mark" that live in a UIElement's CommandRegistry.
  void setCommandFallback(std::function<bool(std::string_view)> fn) { command_fallback_ = std::move(fn); }

  // Read-only counterpart to sendCommand()'s fallback chain: every known
  // command name starting with `prefix`, from commands_ below plus
  // whatever setCommandCompleter() reaches - the M-x minibuffer
  // (StatusLine) uses this for autocomplete, never to execute anything. A
  // set, not a list: Controller's own names and whatever the fallback
  // reaches are two independent sources that could in principle name the
  // same command, and ordered so a later phase can show the candidates
  // sorted without a separate sort step.
  std::set<std::string> commandCompletions(std::string_view prefix) const;
  void setCommandCompleter(std::function<std::set<std::string>(std::string_view)> fn) {
    command_completer_ = std::move(fn);
  }

  // Same IoC shape as the fallback/completer above: lets the UI layer
  // learn whenever songs_/active_buffer_name_ changes (addBuffer()/
  // renameActiveBuffer()/switchToBuffer()/killActiveBuffer(), every path
  // that can move which buffer is active or rename one - not just the
  // ones UI.cpp itself calls directly, but also e.g. a Buffers-menu click
  // straight on a buffer's own row, which resolves entirely inside
  // Controller's own commands_ via refreshBufferCommands()'s
  // "switch-to-buffer:<name>" entries and so never otherwise reaches UI).
  // A single listener call site here beats one refreshBuffers() call
  // scattered at every UI-side buffer command - the exact kind of call
  // site that's easy to add a new path without remembering.
  void setBufferChangeListener(std::function<void()> fn) { buffer_change_listener_ = std::move(fn); }

  // "toggle-record-arm"'s own drum-machine-track repurposing (Session View
  // focused, the targeted clip's track is a DrumMachineTrack) calls this
  // right after focusing the clip (setFocusedClip()) - `opened` true - or
  // right after clearing it again (a second press on the clip already
  // open for editing toggles it back off - clearFocusedClip()) - `opened`
  // false. Lets UI move the shared track cursor and every connected
  // Launchpad's own display to actually show that clip's own step grid
  // (or hand it back to Session view once editing ends), the same
  // callback-not-reaching-into-UI pattern setBufferChangeListener() above
  // already uses (Controller has no idea PatternEditor/LaunchpadManager
  // exist either). Wired in UI::start().
  void setDrumEditRequestListener(std::function<void(int track_id, bool opened)> fn) { drum_edit_requested_ = std::move(fn); }

  std::shared_ptr<AudioBuffer> startRecording() {
    current_sample = std::make_shared<AudioBuffer>(1, 0);
    return current_sample;
  }

  void stopRecording() { current_sample.reset(); }
  bool isRecording() const { return current_sample.get() != nullptr; }
  // Per-buffer (which track is armed follows whichever buffer is active -
  // see recording_track_ids_' own comment): a live mirror, swapped for the
  // active buffer's own saved value on every switchToBuffer()/addBuffer()/
  // killActiveBuffer(), same as getPlaybackInfo()/setPatternSelectionActive()
  // below.
  int getRecordingTrackId() const { return recording_track_id; }
  void setRecordingTrackId(int track_id) { recording_track_id = track_id; }
  const AudioBuffer & getCurrentSample() const { return current_sample ? *current_sample : empty_sample; }
  void addToSample(const AudioBuffer & other) {
    if (current_sample) current_sample->append(other);
  }

  // Snapshots where this take actually starts, synchronously, on the UI
  // thread - UI::handleThresholdRecordingTriggeredEvent() calls this once
  // loudness actually crosses the threshold, at the backdated position the
  // pre-roll ring buffer's own span implies (auto-starting playback right
  // afterward when it wasn't already running doesn't move the position,
  // so the take is just as latency-compensable either way). Overwrites
  // whatever an earlier take may have left behind - a fresh call every
  // time capture starts, never a stale leftover. beginSampleCapture()
  // below reads this back for placement; -1/-1 (the construction-time
  // default - "no position to place at") means unplaced, for anything
  // that creates a recording clip without ever calling this.
  void armRecordingStart(int scene, int row) { recording_start_scene_ = scene; recording_start_row_ = row; }
  // Whether this take has a snapshotted start position at all - UI::
  // handleRecordEvent()'s own guard against creating the clip lazily,
  // uncompensated, for a take that's actually waiting on
  // handleRecordingLatencyEvent() to do it properly instead.
  bool isRecordingArmed() const { return recording_start_scene_ >= 0; }

  // Loudness-threshold-armed recording (a SampleTrack's own Record Arm) -
  // waiting for input to actually cross a threshold before the take
  // genuinely begins. Player.cpp's poll loop reads isThresholdArmed()
  // directly (audio-thread-side, same as isRecording()) to decide whether
  // to keep capture running and feed its own pre-roll ring buffer;
  // armThresholdRecording()/disarmThresholdRecording() are the
  // user-facing arm/cancel pair ("toggle-record-arm"'s own SampleTrack
  // branch), clearThresholdArmed() is the audio-thread-triggered "the arm
  // phase is over, genuine recording has begun instead" transition (UI::
  // handleThresholdRecordingTriggeredEvent()) - two distinctly-named call
  // sites for the identical underlying flag flip, since each means
  // something different to whoever's reading the call, not two different
  // mechanisms. Sets recording_track_id, the same field
  // setRecordingTrackId() already maintains - only one take (threshold-
  // armed or otherwise) can ever be in progress at once.
  void armThresholdRecording(int track_id) { threshold_armed_ = true; recording_track_id = track_id; }
  void disarmThresholdRecording() { threshold_armed_ = false; }
  void clearThresholdArmed() { threshold_armed_ = false; }
  bool isThresholdArmed() const { return threshold_armed_; }

  // The note-track ("everything but SampleTrack") counterpart to the
  // three methods above - "toggle-record-arm"'s own generic branch. A
  // plain global flag, not per-buffer-mirrored, same reasoning
  // threshold_armed_ already has: Record Arm is a single "what's
  // currently being captured" concept, not a per-song one. Consulted by
  // LaunchpadManager (Launchpad-grid note capture, Session-view
  // assign-recording) - the note-grid's own arm/disarm side effects
  // (clearing Session-view audition state, starting/stopping the
  // transport) live there, reacting to this flag's own rising/falling
  // edge each refresh(), not here.
  void armNoteCapture() { note_capture_armed_ = true; }
  void disarmNoteCapture() { note_capture_armed_ = false; }
  bool isNoteCaptureArmed() const { return note_capture_armed_; }

  // Which terminal UI element "toggle-record-arm"'s own arm-something-new
  // branch should target - kept current by UI::renderComponents(), every
  // frame, alongside setSessionViewCursor() below (never read by
  // LaunchpadManager directly, which never needs to know: a connected
  // device's own CC19 press dispatches "toggle-record-arm" the same one
  // way regardless, and by the time it runs, UI has already pushed
  // whatever's actually true here). True means "populate the clip slot
  // named below directly - no arrangement placement, no transport start"
  // instead of the ordinary transport-tied/arrangement-placing behavior.
  void setSessionViewFocused(bool focused) { session_view_focused_ = focused; }
  bool isSessionViewFocused() const { return session_view_focused_; }
  // Session View's own current cursor (track id + Song::getClips(track_id)
  // index) - meaningless unless isSessionViewFocused() is also true.
  void setSessionViewCursor(int track_id, int clip_index) { session_view_track_id_ = track_id; session_view_clip_index_ = clip_index; }

  // Whether the take currently armed/recording is a Session View one -
  // "toggle-record-arm"'s own arm-something-new branch captures
  // isSessionViewFocused()/the cursor above into these the moment it arms,
  // so a later focus or cursor change can't retroactively change an
  // already-in-progress take's own target. -1/-1 (the construction-time
  // default) alongside false names no target.
  bool isSessionRecording() const { return session_recording_; }
  int getSessionRecordingTrackId() const { return session_recording_track_id_; }
  int getSessionRecordingClipIndex() const { return session_recording_clip_index_; }

  // Which track/clip a note-based Session View take just finished into -
  // set once, the moment trimSessionRecordingClip() runs (toggle-record-arm's
  // own disarm branch), since session_recording_track_id_/
  // session_recording_clip_index_ themselves are already reset by the time
  // LaunchpadManager's own refresh() next runs. A "consume once" pair,
  // read and cleared together - finishing a take is a one-time event, not
  // something to keep re-reporting on every future frame just because
  // nothing else has overwritten it since.
  struct CompletedSessionRecording { int track_id; int clip_index; };
  std::optional<CompletedSessionRecording> takeCompletedSessionRecording() {
    if (completed_session_recording_track_id_ < 0) return std::nullopt;
    CompletedSessionRecording result{ completed_session_recording_track_id_, completed_session_recording_clip_index_ };
    completed_session_recording_track_id_ = -1;
    completed_session_recording_clip_index_ = -1;
    return result;
  }

  // Overrides where ensureSessionRecordingClip()'s own first call would
  // otherwise derive this take's row 0 from (previousBarRow() of that
  // call's own absolute step) - for a take that arms into a track already
  // playing an earlier one, LaunchpadManager already knows the exact step
  // the old clip actually stops at (the shared session-quantization grid's
  // own next boundary, not necessarily a multiple of rows_per_bar from
  // absolute row 0 the way previousBarRow() assumes), and this take's own
  // row 0 has to be exactly that step, not wherever previousBarRow() would
  // otherwise place it. A no-op call (arming into an empty/silent slot)
  // simply never calls this, leaving ensureSessionRecordingClip() to
  // derive the origin itself as usual.
  void primeSessionRecordingOrigin(int absolute_step_origin) { session_recording_origin_step_ = absolute_step_origin; }

  // Begins a real Clip for the take currently in progress - creates it,
  // shares its SampleContent buffer with current_sample (so every later
  // addToSample() call is visible through the clip automatically, no
  // separate plumbing needed), and places it as an arrangement instance
  // at armRecordingStart()'s own snapshotted position, if one was taken -
  // never a fresh getPlaybackInfo() read here, which by the time this
  // runs reflects wherever the transport has since moved on to, not
  // where the take actually began. `latency_frames` (0 for an
  // uncompensated/freeform take) is the round-trip delay to trim off the
  // clip's own in-point, resolved to seconds against this Controller's
  // own output rate; remembered (recording_latency_frames_) so
  // finishSampleCapture() can subtract it from the final frame count too,
  // not just the in-point.
  //
  // Two callers, each covering one case: UI::handleRecordEvent() calls
  // this lazily, with latency_frames 0, the first time real audio
  // actually arrives for a take that was *never* armed (no position was
  // ever snapshotted for it) - a clip created with a still-empty
  // (0-frame) buffer would show nothing anyway, so creating it any
  // earlier than "there is actually something to show" bought no real
  // visibility benefit. UI::handleRecordingLatencyEvent() calls this
  // instead, with the real measured latency, for an armed take - as soon
  // as that measurement arrives (see its own comment for why that's
  // already effectively "as early as possible", not a regression from
  // the lazy path). Either way this only ever actually runs once per
  // take (hasRecordingClip() already true skips it) - remembers the new
  // clip's own stable id (recording_clip_id_) so finishSampleCapture()
  // can find it again by id, never by list position. A no-op if track_id
  // doesn't resolve or current_sample is empty (the caller is expected
  // to call addToSample() first - see UI::handleRecordEvent()).
  void beginSampleCapture(int track_id, int latency_frames = 0);

  // Whether beginSampleCapture() has already created this take's own
  // clip - UI::handleRecordEvent()/handleRecordingLatencyEvent()'s own
  // shared guard against calling it more than once per take.
  bool hasRecordingClip() const { return !recording_clip_id_.empty(); }

  // Ends a mic-capture take - "toggle-record-arm"'s own SampleTrack
  // branch calls this once already recording.
  // Finalizes the clip beginSampleCapture() already created, if any (its
  // own real length - total captured frames minus whatever latency was
  // already trimmed off the front, now that the final frame count is
  // known - if no audio ever actually arrived this take, beginSampleCapture()
  // was never called at all (hasRecordingClip() still false), so there's
  // nothing to finalize or clean up either, the same "nothing captured,
  // nothing happens" no-op this had before eager creation was tried and
  // dropped. Always calls stopRecording() to clear current_sample/
  // recording_track_id, and resets armRecordingStart()'s own snapshot back
  // to unarmed so a later take that's never armed at all doesn't
  // accidentally inherit this one's position.
  void finishSampleCapture();

  EventQueue & getUIEventQueue() { return ui_event_queue; }
  EventQueue & getPlaybackEventQueue() { return playback_event_queue; }

  // Audio thread -> VisualizationThread only (see VisualizationThread.h) -
  // carries raw AudioBlockEvents, never anything UI-facing; results come
  // back the other way via ui_event_queue (VisualizationResultEvent).
  EventQueue & getVisualizationQueue() { return visualization_queue; }

  // Plain overwrite - used by togglePlaying()/moveEditPosition()/
  // setEditPosition() for their own optimistic local updates (which must
  // always win immediately, no reconciliation needed) as well as anywhere
  // else that just wants to seed the mirror directly. Per-buffer: this is
  // a live mirror of whichever buffer is currently active, swapped for its
  // own saved copy (playback_infos_) on every switchToBuffer()/addBuffer()/
  // killActiveBuffer() - see saveActiveBufferState()'s own comment. Always
  // targets the *active* buffer specifically - a real Player-thread
  // snapshot for some other, non-active buffer (e.g. one still playing in
  // the background) goes through receivePlaybackSnapshot() instead, which
  // routes it into that buffer's own playback_infos_ map slot directly
  // rather than through this live scalar at all.
  void setPlaybackInfo(const PlaybackInfo & info) { playback_info = info; }
  const PlaybackInfo & getPlaybackInfo() const { return playback_info; }

  // The one path a real Player-thread snapshot (PlaybackEvent, forwarded
  // by UI::handlePlaybackEvent()) should come in through, instead of
  // setPlaybackInfo() directly. Player now pushes one snapshot per live
  // SongState every block (see the per-buffer editing/playback-state
  // plan's Part B), not just one for "the" song, so `buffer_name` (from
  // PlaybackEvent::getBufferName()) says which buffer this is a snapshot
  // of. A snapshot for any buffer *other* than the active one just
  // overwrites its own playback_infos_ map slot outright - moveEditPosition()/
  // setEditPosition() only ever run against the active buffer, so there's
  // no local edit-position prediction that could go stale for any other
  // one. For the active buffer specifically: a snapshot generated by the
  // audio thread's own per-block render before it drained a just-pushed
  // MOVE_POSITION/SET_POSITION control event is stale for the edit-position
  // fields specifically (absolute/pattern/row) - applying it would clobber
  // a more recent local prediction moveEditPosition()/setEditPosition()
  // already made, then get silently corrected again once a caught-up
  // snapshot arrives: a visible cursor jump-back-then-forward. Detected via
  // PlaybackInfo::getPositionEditSeq() vs. this Controller's own
  // local_position_edit_seq_ (bumped once per moveEditPosition()/
  // setEditPosition() call, mirroring SongState::getPositionEditSeq(),
  // which that same buffer's live SongState also bumps on every real
  // per-row playback advance, not just on a seek) - everything else in
  // `info` (voice counts, is_playing, meters, ...) is always accepted
  // as-is regardless, same as a plain setPlaybackInfo() would.
  //
  // local_position_edit_seq_ is per-buffer, same swap-on-switch shape as
  // playback_info/getPlaybackInfo() above (see saveActiveBufferState()'s
  // own comment) - it has to track whichever buffer's own live SongState
  // it's being compared against, and since Part B of the per-buffer
  // editing/playback-state plan that's one independent SongState per
  // buffer, each with its own getPositionEditSeq() counter starting fresh
  // at 0. A single counter shared across every buffer would drift out of
  // sync with whichever buffer is currently active - e.g. arrow-key
  // navigation on one buffer bumping the counter past what a different,
  // freshly-live buffer's own SongState has reached, making every real
  // snapshot for that buffer look permanently stale until playback
  // advanced enough rows to catch back up (confirmed: this is what made
  // the playhead/info line stop updating after starting playback).
  void receivePlaybackSnapshot(const std::string & buffer_name, const PlaybackInfo & info);

  ChannelConfiguration getChannelConfiguration() const { return channel_config; }

  // Process-wide decoder choice, independent of any particular song (any
  // song can be rendered through any decoder - see MixerType.h). Only
  // meaningful when getChannelConfiguration().getType() == AMBISONIC - a
  // MONO config never attempts binaural decoding regardless of this
  // setting (see MixerFactory.cpp).
  MixerType getMixerType() const { return mixer_type_; }
  void setMixerType(MixerType mixer_type) { mixer_type_ = mixer_type; }

  // AMBISONIC_BINAURAL has two underlying implementations - MagLS
  // (AmbisonicMagLSDecoder, the default) and the older virtual-speaker
  // rig (AmbisonicBinauralMixer) - without a third MixerType value for it
  // (see MixerType.h's own comment on why): this is a separate, orthogonal
  // toggle, same shape as --stereo's own force_cardioid, reachable via
  // --legacy-binaural (main.cpp). Ignored entirely unless mixer_type_ is
  // actually AMBISONIC_BINAURAL (see MixerFactory.cpp).
  bool getUseLegacyBinaural() const { return use_legacy_binaural_; }
  void setUseLegacyBinaural(bool use_legacy) { use_legacy_binaural_ = use_legacy; }

  bool togglePlaying();

  // Whatever Record Arm currently has active (recording, threshold-armed,
  // or note-capture-armed - the same three "toggle-record-arm" itself
  // checks, same priority order) has nothing left to record into once the
  // transport stops, so togglePlaying() calls this on every transition
  // into the stopped state, not just an explicit "toggle-record-arm"
  // press - a live take is meant to end the moment playback does,
  // regardless of what actually stopped it. Never toggles playback itself
  // (already happening in the caller) - just tears down whichever one is
  // active, exactly like "toggle-record-arm"'s own matching branch, minus
  // that branch's own playback-toggle call.
  void stopAnyActiveRecordArm();

  // Row navigation while stopped (PatternEditor's move-row-up/down,
  // Page Up/Down, note-entry/backspace's own step, kill-region's bounds
  // adjustment, LaunchpadManager's step-entry advance/auto-stop landing
  // spot) goes through here rather than pushing MOVE_POSITION/SET_POSITION
  // directly, for the same reason togglePlaying() above updates its own
  // mirror synchronously instead of only pushing an event: the UI thread's
  // own main loop (TerminalUI::readInput()) drains every currently-buffered
  // terminal keystroke in one tight loop before ever returning to poll()
  // and giving the Player thread's PlaybackEvent round-trip a chance to be
  // read back - so a burst of terminal-generated key auto-repeat (holding
  // Backspace, or a note key, on a non-Kitty terminal) can call this
  // several times before getPlaybackInfo() ever reflects the first move,
  // making every press in the burst read-and-act-on the same stale row
  // while the rows in between are silently skipped. Updating the local
  // PlaybackInfo mirror immediately - not waiting on the async round trip -
  // means each press in a burst sees the previous one's result right away,
  // exactly like togglePlaying()'s existing synchronous flip. The
  // corresponding MOVE_POSITION/SET_POSITION event is still pushed, purely
  // to keep the audio thread's own SongState in sync for when playback
  // actually starts.
  void moveEditPosition(int delta_rows);
  void setEditPosition(int absolute_row);

  // Whether a pattern-editor selection (mark) is currently open -
  // PatternEditor mirrors its own selection_active_ here on every change
  // (set-mark, kill-region/kill-ring-save's own clear, keyboard-quit, the
  // auto-clear on playback start/pattern crossing in its render()).
  // moveEditPosition()/setEditPosition() only clamp row navigation to the
  // current pattern (Song::clampRowToCurrentPattern() - keeps a mark from
  // ending up stranded in a different pattern than the cursor) while this
  // is true; with no selection open there's nothing to strand, so
  // navigation crosses pattern boundaries freely, the same way playback's
  // own row-by-row advance (SongState::movePosition()) already does.
  // Per-buffer, same swap-on-switch shape as getPlaybackInfo()/
  // getRecordingTrackId() above - a selection open in one buffer shouldn't
  // silently constrain navigation in a different one you've since switched
  // to.
  void setPatternSelectionActive(bool active) { pattern_selection_active_ = active; }

  // Which clip (if any) is currently focused for editing, independent of
  // scene position/instance placement - ArrangementOps.h's
  // resolveEditTarget()/resolveReadTarget() take this as an override.
  // Empty string means no focus. Only one clip is ever focused at a
  // time, song-wide, not one per track - a clip's own id is already
  // globally unique across the whole song (Song::generateUniqueClipId()),
  // so resolveEditTarget()/resolveReadTarget() naturally apply this only
  // to whichever track's own clip list actually contains it, falling
  // through to ordinary resolution for every other track without this
  // needing to be keyed by track_id itself. Matches the actual use case
  // (pick one clip to look at/edit) rather than letting a clip stay
  // focused on some other track indefinitely after attention has moved
  // on - the same reason PatternEditor's own cursor is only ever on one
  // track at a time. Per-buffer (like getRecordingTrackId() above);
  // never serialized - purely live editing-session state.
  //
  // This is deliberately not the same thing as Session-view style clip
  // *launching* (real multi-track simultaneous performance playback,
  // LaunchpadManager::handleSessionPadEvent()/triggerClipStep()) - a
  // focus is a single, exclusive "what am I currently looking at to
  // edit" pointer, so setting a new one always silences whatever the
  // *previous* focus was actively previewing first (stopFocusedClipPreview()),
  // even when that was on a different track, rather than leaving multiple
  // tracks' worth of preview audio stacking up.
  std::string getFocusedClip() const { return focused_clip_id_; }
  // The focused clip's own leaf track - kept alongside the id purely so
  // a focus change can silence the *previous* focus's track without a
  // reverse clip-id -> track-id scan (every getClips(track_id)-based
  // resolution elsewhere already gets track_id from its own caller and
  // has no need of this). -1 when nothing is focused.
  int getFocusedClipTrackId() const { return focused_clip_track_id_; }
  void setFocusedClip(int track_id, const std::string & clip_id) {
    if (track_id != focused_clip_track_id_ || clip_id != focused_clip_id_) stopFocusedClipPreview();
    focused_clip_track_id_ = track_id;
    focused_clip_id_ = clip_id;
  }
  void clearFocusedClip() {
    stopFocusedClipPreview();
    focused_clip_track_id_ = -1;
    focused_clip_id_.clear();
  }

  // Single, shared home for "mutate this track's mute/solo/send and keep
  // the already-running playback state in sync" - neither the terminal's
  // `\` key handler nor any Launchpad control (the two ways a user can
  // trigger these today) duplicate this logic; both just resolve which
  // track_id to act on (whichever way is natural for that input source -
  // the shared on-screen cursor, or a Launchpad device's own assigned
  // track) and call these. Returns false (Send setters: no-op) if track_id
  // doesn't name an existing LeafTrack. Each also
  // pushes the matching PlaybackControlEvent so the change actually reaches
  // the running SongState, not just the Track model - see
  // InstrumentTrackState's public setMuted/setSolo/setSendA/setSendB/
  // setSendMain.
  bool toggleTrackMuted(int track_id);
  bool toggleTrackSolo(int track_id);

  // Unlike the pair above, applies to any Track (Track::isCollapsed() is
  // generic, not LeafTrack-only) and pushes no PlaybackControlEvent -
  // purely a pattern-grid display toggle (VisibleTrackInfo::collapsed_),
  // never read by the running SongState. Returns the new collapsed state,
  // or false if track_id doesn't name an existing track.
  bool toggleTrackCollapsed(int track_id);

  // value is in dB (a perceptual/log scale, easier to dial a subtle send
  // with than a linear fraction) - -100 or below is a hard "off", matching
  // the same floor LeafTrack.cpp's XML load/save uses. Converted to
  // the linear multiplier LeafTrack/SendLevels.h actually store right
  // here, before either the model or the PlaybackControlEvent ever see it.
  void setTrackSendA(int track_id, float value);
  void setTrackSendB(int track_id, float value);
  void setTrackSendMain(int track_id, float value);
  void setTrackAzimuth(int track_id, float value);

  // Note columns (chord/polyphony width, VisibleTrackInfo::num_subtracks_)
  // are otherwise purely derived from actual note data (see Pattern::
  // getTrackInformation()) - these two adjust LeafTrack's own
  // minNoteColumns floor that derivation also takes the max against, a
  // manual override so an empty column can be added ahead of typing into
  // it. No PlaybackControlEvent (unlike the setters above):
  // this only affects display/editing, never audio - the running SongState
  // never reads it.
  void addNoteColumn(int track_id);
  void removeNoteColumn(int track_id);

  // Sets slot 0 (A) or 1 (B) of the shared send bus (Song::setBusSlotKind())
  // to a fresh, default-parameter instance of `kind`, and - same reasoning
  // as toggleTrackMuted()/setTrackSendA() above - pushes the matching
  // PlaybackControlEvent so an already-running SongState's own live
  // SendBusProcessor slot is swapped too, not just the Track model: unlike
  // those, this needs no LeafTrack resolution and always applies.
  void setBusEffectKind(int slot, BusEffectKind kind);

  // Single, shared home for the whole-row-replace sweep a realtime
  // recording session (auto-record-while-held, both the terminal keyboard
  // path in PatternEditor and the Launchpad pad path in LaunchpadManager)
  // uses to make a fresh take overwrite a row's old content rather than
  // merging into it. Idempotent per (row, track_id) against `cleared_rows`
  // - `insert().second` is false once a pair's already been cleared this
  // session - so every live-input write site can call this defensively
  // without worrying about which one gets there first or double-clearing.
  // `cleared_rows` (and when it resets) stays owned by the caller rather
  // than moving in here too: PatternEditor's and LaunchpadManager's
  // recording sessions are independent and can be active at the same time
  // (different tracks), so sharing one set here would let one session's
  // end (which clears its own bookkeeping) reset the other's mid-session
  // and cause a stray re-clear that wipes notes the other session already
  // wrote this take.
  void ensureRowCleared(std::set<std::pair<int, int>> & cleared_rows, int pattern_idx, int row, int track_id);

  // Sweeps ensureRowCleared() over every row the transport has newly
  // passed through since the last call, for every track named in
  // `track_ids` - the onRowAdvanced() half of the realtime auto-record
  // session, shared by PatternEditor and LaunchpadManager the same way
  // ensureRowCleared() itself is (see its own comment for why the session
  // bookkeeping stays owned by the caller). Resyncs to just `new_row`
  // rather than trying to backfill a range, if the pattern changed or the
  // row went backwards (a loop/pattern-sequence wraparound) - a range
  // spanning that boundary has no single well-defined meaning. `track_ids`
  // stays a caller-computed parameter rather than something this method
  // resolves itself: PatternEditor derives it from active_keyboard_notes_,
  // LaunchpadManager unions it across every device's own active_notes -
  // different data structures per input source, not shareable here.
  void sweepAutoRecordRows(std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int & last_cleared_pattern_idx, int pattern_idx, int new_row, const std::vector<int> & track_ids);

  // Called once per PlaybackEvent (UI::handlePlaybackEvent(), right
  // alongside the two onRowAdvanced() calls above) while `recording` is
  // true (the caller's own union of PatternEditor::isAutoRecording()/
  // LaunchpadManager::isAutoRecording() - a single shared model-level
  // concern, not tied to which input source is actually recording) and
  // the transport is playing: if the currently-playing scene
  // (PlaybackInfo::getPatternIndex()) is already in its own last bar,
  // grows it by one more (Scene::setLengthBars()) - keeping it
  // comfortably ahead of the actual playhead for as long as recording
  // continues (this runs far more often than once per bar at any
  // reasonable tempo), so a live take is never confined to a fixed
  // pre-existing length the way ordinary (non-recording) playback still
  // is. A no-op while stopped or not recording - ordinary note entry
  // never needs this (PatternEditor's cursor navigation already can't
  // reach a row past the current scene's own bounds).
  void extendRecordingSceneIfNeeded(bool recording);

  // Generalizes beginSampleCapture()'s own lazy-creation precedent to
  // PatternEditor's/LaunchpadManager's realtime held-note recording: a
  // live take should write into a real, individually-manageable Clip
  // instance, the same as Session-view's own pooled clips, not directly
  // into the scene's own background Pattern with no identity of its own.
  // Called right before a live take's own note write, at (track_id, row) -
  // a no-op if a real clip is already active there (resolveInstanceAt()),
  // or if `focused_clip_id` (Controller::getFocusedClip()) overrides
  // resolution entirely - either way the write already lands somewhere
  // real without this. An explicit stop is treated the same as nothing
  // placed at all here (resolveEditTarget()'s own convention already
  // treats the two identically) - it has no Pattern of its own to write
  // into either. `clip_ids` is caller-owned and passed by reference, same
  // reasoning ensureRowCleared()'s own `cleared_rows` parameter already
  // has - PatternEditor's and LaunchpadManager's own recording sessions
  // are independent, so one's own map must never let the other's session
  // affect it.
  void ensureNoteRecordingClip(std::unordered_map<int, std::string> & clip_ids, int track_id, int pattern_idx, int row);

  // Clip::setLength()'s own counterpart to extendRecordingSceneIfNeeded()
  // above - grows a note-recording clip's own window the same way, once
  // the currently-playing row is near its own end, so a long live take is
  // never silently dropped back to the background Pattern mid-take
  // (resolveInstanceAt()'s own one-shot-expiry check would otherwise stop
  // considering it active). Same per-row-advance call site
  // (UI::handlePlaybackEvent(), alongside extendRecordingSceneIfNeeded()
  // itself) - just a different target, and deliberately *not* the same
  // trigger condition: extendRecordingSceneIfNeeded() is gated on
  // isAutoRecording() (did *this* caller's own session start the
  // transport - stays false if the performer had already started
  // playback manually, e.g. from row 0, before ever arming/holding a
  // note), but a clip this method already knows it created
  // (`clip_ids` non-empty) needs no such gate at all - its own existence
  // already proves a genuine live-recording write put it there, entirely
  // independent of who happened to start the transport. Gating on
  // isAutoRecording() here too would silently stop growing exactly the
  // "record from the very start of the song" take that scenario
  // describes, real playback quietly outrunning a clip nothing is
  // extending any more. `clip_ids` names only the clips *this* caller's
  // own session created (ensureNoteRecordingClip() above) - never a
  // pre-existing, unrelated clip the transport merely happens to be
  // passing over while some other track's session is active, which would
  // otherwise get its own authored length silently mutated every time it
  // loops back around near its end. `held_track_ids` (PatternEditor's/
  // LaunchpadManager's own getActiveNoteTrackIds()) further scopes growth
  // to only a track with a note actually held right now - Record Arm has
  // no auto-stop-on-release the way a keyboard session does, so a clip
  // left ungated on this would otherwise keep growing (and clearing
  // everything in its own path) for as long as the session stays armed,
  // long after the performer actually stopped playing anything.
  void extendRecordingClipsIfNeeded(std::unordered_map<int, std::string> & clip_ids, const std::vector<int> & held_track_ids);

  // ensureNoteRecordingClip()/extendRecordingClipsIfNeeded()'s own twin for
  // Session View recording (isSessionRecording()) - writes directly into
  // getSessionRecordingTrackId()'s own clip at getSessionRecordingClipIndex()
  // (creating it on the first call, same "overwrite in place" rule
  // beginSampleCapture() already applies for the SampleTrack case if that
  // index already holds a clip - see its own comment), never calling
  // placeClipInstance() at all. `absolute_step` is the audition clock's own
  // raw step (LaunchpadManager's own audition_clock_.currentStep(), a
  // Launchpad's NOTE grid being the only way notes reach here today -
  // keyboard note *recording* not being viable without key-release events
  // this terminal doesn't deliver), not getPlaybackInfo().getRowIndex() - a
  // session recording can run with the transport stopped, so there's no
  // live global position to read.
  //
  // The *first* call for a take is where session_recording_origin_step_
  // itself gets established, unless primeSessionRecordingOrigin() already
  // fixed it - snapped back to that bar's own start, not the exact step
  // this first note happened to land on: a performer may deliberately
  // start playing on the bar's second beat rather than its first, and the
  // clip's own row 0 still has to be the bar's start either way, not
  // wherever they first happened to play. `grid_origin_step` is
  // LaunchpadManager's own session_origin_step_ - the shared bar-boundary
  // reference every other track's own Session View clip is already
  // measured against - when something else in the session is already
  // playing (-1 otherwise, meaning "nothing else established one yet");
  // snapping is measured relative to *that* (ArrangementOps.h's
  // previousBarRow() of `absolute_step - grid_origin_step`, itself offset
  // back by grid_origin_step) rather than absolute step 0 in that case, so
  // a take recorded onto a fresh track while something else already loops
  // still lands in the same shared groove once it starts repeating,
  // instead of drifting against it by whatever `grid_origin_step` itself
  // isn't a multiple of the bar length. Every call, first or not, returns
  // this take's own row (absolute_step - session_recording_origin_step_) -
  // -1 whenever getSessionRecordingClipIndex() is out of range (nothing
  // armed).
  int ensureSessionRecordingClip(int absolute_step, int grid_origin_step = -1);
  // Same growth-loop shape as extendRecordingClipsIfNeeded() above, keyed
  // off session_recording_origin_step_ (established by
  // ensureSessionRecordingClip() above by the time this is ever
  // meaningful) the same way that method is, rather than the transport's
  // own position - grows the clip a bar ahead of `absolute_step` whenever
  // it's getting close to its own current end. A no-op before
  // ensureSessionRecordingClip() has run at least once this take
  // (session_recording_clip_ready_ still false) - nothing to grow yet.
  void extendSessionRecordingClipIfNeeded(int absolute_step);
  // Finalizes a note-recording Session View take, called once when it
  // actually ends (toggle-record-arm's own disarm branch).
  // extendSessionRecordingClipIfNeeded() above only ever grows the clip a
  // bar ahead of wherever the take currently is, so by the time the
  // performer stops playing and disarms, its length is however far the
  // free-running clock had gotten to, not however much of it actually
  // holds a note - trimmed back down to the last written row, rounded up
  // to that row's own containing bar (a fresh one-bar clip if nothing
  // ever landed). Also flips it to looping and records it via
  // takeCompletedSessionRecording() for LaunchpadManager to pick up - a
  // fresh take is meant to be heard right back, looping, the instant it's
  // done. A no-op unless a clip actually exists for this take
  // (session_recording_clip_ready_).
  void trimSessionRecordingClip();

  // beginSampleCapture()'s own counterpart to extendRecordingClipsIfNeeded()
  // above - same reasoning, same growth shape, but scoped to the one
  // SampleTrack take a mic capture session can ever have in progress
  // (recording_clip_id_/recording_start_scene_/recording_start_row_), so
  // it needs no clip_ids/held_track_ids parameters of its own. A no-op
  // unless hasRecordingClip() and this take was actually placed
  // (recording_start_scene_ >= 0 - an unarmed, freeform take has nothing
  // to grow into either, same as it has nothing to place at all). Same
  // per-row-advance call site as extendRecordingClipsIfNeeded()
  // (UI::handlePlaybackEvent()), ungated on any auto-started-playback
  // flag for the identical reason that method already documents - a live
  // mic take's own existence already proves it's genuine, regardless of
  // who started the transport.
  void extendRecordingSampleClipIfNeeded();

  // Engages the realtime auto-play-while-held session (PatternEditor's
  // keyboard entry and LaunchpadManager's pad entry both offer this):
  // starts the transport and mutes the song's own pattern-driven
  // scheduling (SongState::render()'s own comment has the full reasoning)
  // so only this live take's own PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE stream
  // sounds, then resets the caller's whole-row-replace bookkeeping
  // (`cleared_rows`) and note-recording-clip bookkeeping (`clip_ids`, see
  // ensureNoteRecordingClip()'s own comment) for the fresh session. The
  // caller decides *when* to call this - its own "is this the first held
  // note, and are we not already playing" check (again per-input-source
  // state, not shareable) - so it's only ever called once per session,
  // right before that session's first write.
  void startAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int & last_cleared_pattern_idx, std::unordered_map<int, std::string> & clip_ids);

  // Starts the transport for Session-view clip-trigger recording
  // (LaunchpadManager::handleSessionPadEvent()'s assign path) - unlike
  // startAutoRecordSession() above, does *not* mute the song's own
  // pattern-driven scheduling (SET_RECORDING_MUTE). A held note has a
  // separate live PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE stream to be heard
  // through while old content stays muted; a triggered clip has no such
  // separate path - the instant placeClipInstance() places it, it *is*
  // the song's own scheduled content, so muting that scheduling would
  // silence the very thing being recorded. Also skips the whole-row-clear
  // bookkeeping reset startAutoRecordSession() does - clip-instance
  // placement already clears any overlapping instance synchronously at
  // press time (ArrangementOps.h's placeClipInstance()), not via a
  // swept-forward-in-time mechanism, so there's nothing here to reset.
  void startAutoRecordPlayback(bool & auto_started_playback);

  // The matching end of startAutoRecordSession(): stops the transport
  // (only if it's still genuinely playing - the user may have manually
  // stopped it mid-hold already, and toggling again here would incorrectly
  // restart it) and lands the cursor just past the final note-off this
  // take wrote, then unmutes the song's own scheduling and clears the
  // session flag/bookkeeping unconditionally either way, so a manual
  // mid-hold stop never leaves recording muted or the flag stuck true.
  // The caller decides when the session is over (its own "last held note
  // just released" check) and passes its current PlaybackInfo snapshot so
  // the landing position is computed from the same snapshot the check
  // itself saw, not a value that may have drifted by the time this runs.
  // `clip_ids` (see ensureNoteRecordingClip()'s own comment) is cleared
  // unconditionally here too, not required for correctness (the next
  // session's own start resets it too) - just don't hold onto a finished
  // session's bookkeeping longer than needed.
  void stopAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, const PlaybackInfo & info, std::unordered_map<int, std::string> & clip_ids);

  // Writes an explicit note-off at `row` for a live take's release, once
  // the transport has moved past the note's own row - shared tail of
  // PatternEditor::offerInput()'s and LaunchpadManager::handlePadEvent()'s
  // RELEASE handling (mirroring handleMidiEvent()'s own NOTE_OFF write).
  // In this tracker's own pattern model, a single line can't hold both a
  // note and its own note-off, so the caller only calls this once it's
  // confirmed `row` isn't still the note's own row - writing here
  // unconditionally would erase the note it belongs to instead of ending
  // it. Sweeps the row clean first (like every other live write site) when
  // this caller's own session started the transport.
  void writeReleaseOff(std::set<std::pair<int, int>> & cleared_rows, bool auto_started_playback, int pattern_idx, int row, int track_id, int note_column, int delay);

  // Applies a pressure/aftertouch update to an already-written note -
  // shared by PatternEditor::handleMidiEvent()'s NOTE_PRESSURE handling
  // (physical MIDI keyboard input) and LaunchpadManager::handlePadEvent()'s
  // AFTERTOUCH handling. Leaves the caller to bump the song version or set
  // its own row_edited flag afterward, whichever that input path already
  // uses (the two aren't unified - see the one-row partial redraw's own
  // reasoning elsewhere), and to resolve which row/rate-limiting rules
  // apply before calling this - only the actual read-modify-write of the
  // note itself is shared.
  void applyNotePressure(int pattern_idx, int row, int track_id, int note_column, short velocity, int delay);

  // Emacs prefix-argument style: transient, one-shot context a caller (the
  // Launchpad command-dispatch path, UI::handleLaunchpadButtonEvent) sets
  // right before invoking a named command by string (executeCommand()),
  // for whichever registered command actually wants it (currently
  // "toggle-mute"/"toggle-solo", resolving which track a specific
  // Launchpad device is following - see PatternEditor's constructor) - the
  // caller doesn't need to know which commands care, and a command that
  // doesn't consume it simply leaves it to be overwritten/cleared by the
  // next dispatch. consumePendingCommandTrack() reads and clears in one
  // step, exactly like reading Emacs's current-prefix-arg resets it - so a
  // stale value can never leak into a later, unrelated command (e.g. one
  // invoked from a keybinding or M-x, which never sets this at all and
  // always gets the caller-supplied fallback instead).
  void setPendingCommandTrack(int track_id) { pending_command_track_ = track_id; }
  int consumePendingCommandTrack(int fallback) {
    if (pending_command_track_ < 0) return fallback;
    int track_id = pending_command_track_;
    pending_command_track_ = -1;
    return track_id;
  }

  // The one global octave that drives computer-keyboard note entry
  // (PatternEditor's toMidiNote() calls) and every connected Launchpad's
  // own octave (relative to this, via LaunchpadManager's per-device
  // offset - see LaunchpadLayout::clampOctave/clampOctaveOffset).
  // Ephemeral editing-session state, like pending_command_track_ above -
  // never serialized into the song.
  int getGlobalOctave() const { return global_octave_; }
  void setGlobalOctave(int octave) {
    global_octave_ = std::clamp(octave, constants::MIN_OCTAVE, constants::MAX_OCTAVE);
  }
  // +/-1, same clamp as setGlobalOctave() above - deliberately not
  // LaunchpadLayout::clampOctave() (identical arithmetic): Controller sits
  // below src/launchpad/, which already depends on Controller, and this
  // one-line clamp isn't worth introducing the reverse edge for.
  void octaveUp() { setGlobalOctave(global_octave_ + 1); }
  void octaveDown() { setGlobalOctave(global_octave_ - 1); }

  const InstrumentProvider & getInstrumentProvider() const { return instrument_provider; }

 private:
  ChannelConfiguration channel_config;
  MixerType mixer_type_ = MixerType::AMBISONIC_STEREO;
  bool use_legacy_binaural_ = false;

  // Every open song, keyed by song id (a real file path, or a
  // freshBufferName()-generated name for a never-yet-saved one) - real
  // storage for every song openSong()/switchToBuffer() have added and
  // killActiveBuffer() hasn't fully closed yet (see open_aspects_by_song_
  // below for which of its aspects are currently open as actual
  // buffer-list entries), not just the active one (see
  // active_buffer_name_ below). Named songs_, not buffers_: this map's
  // value type is literally Song, one per underlying song regardless of
  // how many aspect-views of it are open right now - if a future,
  // non-Song buffer kind shows up, that's the point this stops being
  // accurate and needs revisiting, not before.
  std::map<std::string, std::shared_ptr<Song>> songs_;

  // A song id can be viewed two ways, symmetrically - PatternEditor
  // (note/command editing) and SessionView (the clip-launch overview) -
  // and either can be open, closed, or both at once, independently: the
  // buffer-list entry for a song id's PATTERN_EDITOR aspect is its own id
  // verbatim; SESSION_VIEW's is `id + " [Session]"`
  // (bufferNameFor()/aspectFor() below convert between the two forms).
  // Neither aspect is privileged over the other the way PatternEditor
  // used to be (songs_'s own key was once inseparable from "the
  // PatternEditor buffer") - a song stays open in songs_ as long as
  // *either* aspect has an open view (open_aspects_by_song_[id] is
  // non-empty); killing the last one closes the underlying song too (see
  // killActiveBuffer()). Guarded by song_mutex_ alongside songs_/
  // active_buffer_name_ for the same reason (see that member's own
  // comment) - mutated only on the UI thread.
  enum class BufferAspect { PATTERN_EDITOR, SESSION_VIEW };
  std::map<std::string, std::set<BufferAspect>> open_aspects_by_song_;
  // The " [Session]" suffix marking a buffer-list name as a song id's own
  // SessionView aspect - bufferNameFor()/aspectFor() below are the only
  // two places that ever need to know its exact spelling.
  static constexpr const char * kSessionViewSuffix = " [Session]";
  std::string bufferNameFor(const std::string & song_id, BufferAspect aspect) const {
    return aspect == BufferAspect::SESSION_VIEW ? song_id + kSessionViewSuffix : song_id;
  }
  BufferAspect aspectFor(const std::string & name) const {
    auto suffix_len = std::string(kSessionViewSuffix).size();
    bool has_suffix = name.size() >= suffix_len && name.compare(name.size() - suffix_len, suffix_len, kSessionViewSuffix) == 0;
    return has_suffix ? BufferAspect::SESSION_VIEW : BufferAspect::PATTERN_EDITOR;
  }
  // `name`'s own song id - itself, if it's already one (a PatternEditor
  // aspect's own buffer-list name *is* its song id verbatim), or with the
  // SessionView suffix stripped otherwise. A pure string operation now
  // (no lookup), so - unlike the old alias-map version - it needs no
  // song_mutex_ guard of its own; every other buffer-name-keyed lookup in
  // this class goes through this first rather than re-deriving it.
  std::string canonicalBufferName(const std::string & name) const {
    return aspectFor(name) == BufferAspect::SESSION_VIEW ? name.substr(0, name.size() - std::string(kSessionViewSuffix).size()) : name;
  }
  // openSessionViewBuffer()/openPatternEditorBuffer()'s own shared body -
  // opens (idempotent if already open) `aspect`'s own view of the
  // currently active song and switches to it.
  std::string openAspectBuffer(BufferAspect aspect);
  // hasUnsavedChanges()'s baseline, one per songs_ entry rather than one
  // shared scalar - each buffer's own unsaved-changes state is independent
  // of whichever buffer happens to be active, so switching the active one
  // must never reset or conflate them. Kept in lockstep with songs_ (same
  // key added/removed/renamed together) by addBuffer()/renameActiveBuffer()/
  // killActiveBuffer() below.
  std::map<std::string, Version> last_saved_versions_;
  // Which songs_ entry is current - see getActiveBufferName()/getSong().
  std::string active_buffer_name_;
  // Guards songs_/active_buffer_name_'s own reassignment (addBuffer()/
  // renameActiveBuffer()/switchToBuffer()/killActiveBuffer()) against
  // Player::play()'s getCurrentSong() call on the audio thread - see that
  // method's own comment for the use-after-free this prevents. mutable so
  // a const Controller& can still lock it there. Every other songs_/
  // active_buffer_name_ access (getSong(), getActiveBufferName(), and
  // their many UI-thread callers) needs no lock of its own: they're never
  // concurrent with a reassignment, which is also always on the UI thread.
  mutable std::mutex song_mutex_;
  // Inserts (song, name) as a new songs_ entry and makes it active -
  // openSong()'s own tail once it's read a Song from disk. Never removes
  // any other entry (unlike the single-buffer design this replaced) -
  // that's killActiveBuffer()'s job. `saved_version` seeds
  // last_saved_versions_[name] - always song->getVersion() right after
  // a fresh open, but the caller's job to compute since it must be read
  // before this call, not after.
  void addBuffer(std::shared_ptr<Song> song, const std::string & name, Version saved_version);
  // Renames the active buffer's own songs_/last_saved_versions_ entry to
  // `new_name` (erase old key, insert new, same Song and active either
  // way) - saveSongAs()'s own tail, once it's already called Song::save().
  // `saved_version` is again the caller's job to compute
  // (song->getVersion() right after that save() call) for the same
  // reason addBuffer() takes it as a parameter rather than reading it
  // itself.
  void renameActiveBuffer(const std::string & new_name, Version saved_version);
  // Saves the *currently* active buffer's own live playback_info/
  // recording_track_id/pattern_selection_active_/local_position_edit_seq_/
  // focused_clip_id_ into their map slots - a no-op before any buffer has
  // ever been active, at startup. Called right *before* a caller
  // (addBuffer()/switchToBuffer()/killActiveBuffer() below) reassigns
  // active_buffer_name_ itself under song_mutex_ - kept as its own step
  // rather than folded into a single "set the active buffer" method so it
  // never needs to take that lock itself (these five scalars are
  // UI-thread-only, untouched by the audio thread, so they don't need it
  // - but calling in from inside a caller's own already-held lock_guard
  // would deadlock on a plain, non-recursive std::mutex).
  void saveActiveBufferState();
  // The other half of saveActiveBufferState(): loads `name`'s own map
  // slot into the five live scalars - each defaults freshly the first
  // time any given buffer name is switched to, via plain
  // std::map::operator[] auto-inserting a default-constructed value, so
  // there's no separate "is this a first visit" case to handle. Called
  // right *after* active_buffer_name_ has already been reassigned to
  // `name` (so `name` here is expected to equal it).
  void loadActiveBufferState(const std::string & name);
  // Drops `name`'s own playback_info/recording_track_id/
  // pattern_selection_active_/local_position_edit_seq_/focused_clip_id_
  // map slot entirely - killActiveBuffer()'s own tail (nothing worth
  // keeping for a buffer that's gone) and renameActiveBuffer()'s (the
  // live scalars stay authoritative through a mere rename, untouched by
  // save/loadActiveBufferState(); the *old* key's slot would just be
  // stale dead weight otherwise).
  void dropBufferState(const std::string & name);
  // Keeps one "switch-to-buffer:<name>" CommandRegistry entry per songs_
  // key in sync with it - see refreshBufferCommands()'s own comment on
  // Controller.cpp. Called after every songs_ structural change
  // (addBuffer()/renameActiveBuffer()/killActiveBuffer(), and
  // switchToBuffer()'s own create-if-missing branch).
  void refreshBufferCommands();
  // The buffer names refreshBufferCommands() itself registered last time,
  // so it can tell which ones dropped out of songs_ since and need
  // commands_.undefine()'d - songs_'s own keys alone can't say that, only
  // what's still open now, not what used to be.
  std::set<std::string> registered_buffer_commands_;
  std::shared_ptr<AudioBuffer> current_sample;
  InstrumentProvider instrument_provider;
  EventQueue ui_event_queue, playback_event_queue, visualization_queue;
  // playback_info/recording_track_id/pattern_selection_active_/
  // local_position_edit_seq_/focused_clip_id_ below are each a live
  // mirror of whichever buffer is currently active; these five maps
  // (mirroring last_saved_versions_' own shape, keyed the same way) hold
  // every *other* open buffer's own saved copy. save/loadActiveBufferState()
  // are the one place that swap between a live scalar and its map slot.
  std::map<std::string, PlaybackInfo> playback_infos_;
  std::map<std::string, int> recording_track_ids_;
  std::map<std::string, bool> pattern_selection_actives_;
  std::map<std::string, int> local_position_edit_seqs_;
  std::map<std::string, std::string> focused_clip_ids_;
  std::map<std::string, int> focused_clip_track_ids_;
  PlaybackInfo playback_info;
  // How many position-editing control events moveEditPosition()/
  // setEditPosition() have themselves pushed against the active buffer -
  // compared against that buffer's own incoming snapshot
  // PlaybackInfo::getPositionEditSeq() by receivePlaybackSnapshot() to
  // detect a stale one. See that method's own comment. Per-buffer, same
  // swap-on-switch shape as playback_info above (it has to track whatever
  // buffer's own live SongState::getPositionEditSeq() it's paired
  // against, and that's one independent counter per buffer since Part B
  // of the per-buffer editing/playback-state plan).
  int local_position_edit_seq_ = 0;
  int recording_track_id = 0;
  // The clip beginSampleCapture() created for the take in progress -
  // empty when nothing is being sample-captured right now. Looked up by
  // stable id (never list position - Song.h's own comment on why) when
  // finishSampleCapture() needs to find it again.
  std::string recording_clip_id_;
  // Snapshotted synchronously, on the UI thread, the instant a take
  // actually starts (armRecordingStart()'s own callers) - -1 means "not
  // armed this take" (no compensation to apply), reset to that at the
  // *start* of every take rather than only read once, so a later take
  // never accidentally inherits an earlier one's position. See
  // beginSampleCapture()'s own comment for how these get used.
  int recording_start_scene_ = -1, recording_start_row_ = -1;
  // The round-trip latency (frames) beginSampleCapture() actually trimmed
  // off this take's own in-point, if any (0 for an uncompensated/freeform
  // take) - finishSampleCapture() needs it again to compute the clip's
  // own post-trim length, not the full captured buffer.
  int recording_latency_frames_ = 0;
  // armThresholdRecording()/isThresholdArmed()'s own flag - see their
  // shared doc comment.
  bool threshold_armed_ = false;
  // armNoteCapture()/isNoteCaptureArmed()'s own flag - see their shared
  // doc comment.
  bool note_capture_armed_ = false;
  // Set by "toggle-record-arm"'s own SampleTrack branch when arming a
  // take also had to start the transport itself, so finishing/disarming
  // later knows whether to stop it again.
  bool record_arm_auto_started_playback_ = false;
  // setSessionViewFocused()/setSessionViewCursor()'s own backing fields -
  // see their shared doc comment.
  bool session_view_focused_ = false;
  int session_view_track_id_ = -1, session_view_clip_index_ = -1;
  // isSessionRecording()/getSessionRecordingTrackId()/
  // getSessionRecordingClipIndex()'s own backing fields - see their shared
  // doc comment.
  bool session_recording_ = false;
  int session_recording_track_id_ = -1, session_recording_clip_index_ = -1;
  // ensureSessionRecordingClip()'s own "have I already created/reset this
  // take's own clip" latch - reset to false alongside session_recording_
  // itself (both directions, in "toggle-record-arm"), so the *first* call
  // for a given take creates a fresh clip (an empty slot) or resets an
  // existing one's own Pattern content back to empty (an occupied slot,
  // "overwrite in place" - see that method's own comment), and every later
  // call this same take just finds it already prepared.
  bool session_recording_clip_ready_ = false;
  // A Session View recording take's own "row 0" - the audition clock's own
  // absolute step, snapped back to that bar's own start (previousBarRow(),
  // ArrangementOps.h - the same rounding ensureNoteRecordingClip() already
  // uses for the identical reason), the moment the *first real note*
  // actually arrives (ensureSessionRecordingClip()'s own "not ready yet"
  // branch) - not the moment arming happens. A performer needs time to get
  // ready (or listen to whatever's already playing) before actually
  // playing anything; fixing row 0 at arm time would bake however long
  // that took as dead silence into the front of the clip. -1 (the reset
  // default, alongside session_recording_clip_ready_ above) means not yet
  // established this take.
  int session_recording_origin_step_ = -1;
  // takeCompletedSessionRecording()'s own backing fields - see its shared
  // doc comment.
  int completed_session_recording_track_id_ = -1, completed_session_recording_clip_index_ = -1;
  // See getGlobalOctave()'s own comment - deliberately global, unlike the
  // per-buffer state above.
  int global_octave_ = 4;
  // Controller's own named commands ("save-song", ...) - sendCommand()
  // tries this first, then command_fallback_; commandCompletions() reads
  // its prefix index the same way.
  CommandRegistry commands_;
  std::function<bool(std::string_view)> command_fallback_;
  std::function<std::set<std::string>(std::string_view)> command_completer_;
  std::function<void()> buffer_change_listener_;
  std::function<void(int track_id, bool opened)> drum_edit_requested_;
  int pending_command_track_ = -1;
  bool pattern_selection_active_ = false;
  // Live mirror of the active buffer's own focused_clip_ids_/
  // focused_clip_track_ids_ slots - see getFocusedClip()'s own comment.
  // Swapped the same save/loadActiveBufferState() cycle as the other
  // per-buffer scalars above.
  std::string focused_clip_id_;
  int focused_clip_track_id_ = -1;
  // Silences whatever track the *current* focus (before it's overwritten
  // by setFocusedClip()/clearFocusedClip()) was actively previewing - a
  // plain STOP_ALL_NOTES, the same natural-release convention Session
  // view's own clip stops already use (InstrumentTrackState::
  // stopAllVoices()), not a hard cut. A no-op when nothing was focused.
  void stopFocusedClipPreview();

  static inline AudioBuffer empty_sample;
};

#endif
