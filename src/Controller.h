
#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "audio/AudioBuffer.h"
#include "instruments/InstrumentProvider.h"
#include "playback/EventQueue.h"
#include "state/PlaybackInfo.h"
#include "ambisonic/ChannelConfiguration.h"
#include "ambisonic/MixerType.h"
#include "ui/CommandRegistry.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
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
  // synchronously, regardless of when the audio thread later notices the
  // swap via SONG_CHANGED. Returning an actual shared_ptr copy - under the
  // same mutex every one of those methods takes, see song_mutex_'s own
  // comment - keeps the *old* Song object alive for as long as the audio
  // thread's own copy of the pointer is still in use, however long after
  // the UI thread has moved on to a new one.
  std::shared_ptr<Song> getCurrentSong() const {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto it = songs_.find(active_buffer_name_);
    return it == songs_.end() ? nullptr : it->second;
  }

  // UI-thread convenience wrappers around getCurrentSong() - see that
  // method's own comment for why the audio thread must go through the
  // locked shared_ptr instead. Never concurrent with a reassignment
  // (also always on the UI thread), so no dangling-reference risk here.
  Song & getSong() { return *getCurrentSong(); }
  const Song & getSong() const { return *getCurrentSong(); }

  // Which songs_ entry is "current" - UI-thread-only, like song_mutex_'s
  // other UI-thread-only reads, so no lock needed here either (see
  // song_mutex_'s own comment).
  const std::string & getActiveBufferName() const { return active_buffer_name_; }

  // Every open buffer's name, in name-sorted order - the Buffers menu
  // (TerminalMenu::refreshBuffers()) and any future buffer-listing command
  // read this rather than songs_ directly.
  std::vector<std::string> getBufferNames() const {
    std::vector<std::string> names;
    names.reserve(songs_.size());
    for (auto & [name, song] : songs_) names.push_back(name);
    return names;
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

  // Song::getVersion() (incVersion(), bumped on every structural or note
  // edit - see Song.h/PatternEditor.cpp's own call sites) against a
  // baseline snapshotted whenever the *active* buffer was last freshly
  // created, opened, saved, or switched to. Not a precise "dirty" bit (a
  // mutation path that forgets to call incVersion() would go unnoticed),
  // but reuses an existing, already-pervasive mechanism rather than adding
  // a parallel one - good enough to gate kill-buffer's "discard unsaved
  // changes?" prompt (UI.cpp). See hasAnyUnsavedChanges() below for the
  // all-buffers counterpart save-buffers-kill-terminal needs instead.
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
  // killActiveBuffer() - see saveActiveBufferState()'s own comment. The audio
  // thread itself doesn't know or care about buffers yet (Player still
  // rebuilds its one SongState from scratch on every switch), so a
  // snapshot arriving right after a switch will still promptly overwrite
  // whatever position/row/pattern this restores with the freshly-rebuilt
  // song's own (zeroed) state; is_playing/voice-count fields already read
  // correctly per-buffer even so, since those really do reflect "what's
  // this buffer doing right now."
  void setPlaybackInfo(const PlaybackInfo & info) { playback_info = info; }
  const PlaybackInfo & getPlaybackInfo() const { return playback_info; }

  // The one path a real Player-thread snapshot (PlaybackEvent, forwarded
  // by UI::handlePlaybackEvent()) should come in through, instead of
  // setPlaybackInfo() directly. A snapshot generated by the audio thread's
  // own per-block render (state_.renderBlock() runs on every audio callback
  // regardless of play state) before it drained a just-pushed
  // MOVE_POSITION/SET_POSITION control event is stale for the edit-position
  // fields specifically (absolute/pattern/row) - applying it would clobber
  // a more recent local prediction moveEditPosition()/setEditPosition()
  // already made, then get silently corrected again once a caught-up
  // snapshot arrives: a visible cursor jump-back-then-forward. Detected via
  // PlaybackInfo::getPositionEditSeq() vs. this Controller's own
  // local_position_edit_seq_ (bumped once per moveEditPosition()/
  // setEditPosition() call, mirroring SongState::getPositionEditSeq(),
  // bumped once per processed control event) - everything else in `info`
  // (voice counts, is_playing, meters, ...) is always accepted as-is
  // regardless, same as a plain setPlaybackInfo() would.
  //
  // local_position_edit_seq_ itself deliberately stays a single global
  // counter, not per-buffer like playback_info/getPlaybackInfo() above,
  // even though it's paired with per-buffer state: it has to keep matching
  // whatever SongState::getPositionEditSeq() the audio thread reports, and
  // that counter is global too (the same reused SongState instance across
  // every switch, not one per buffer) - making just one side of this
  // comparison per-buffer while the other stays global would make every
  // snapshot for a freshly-restored buffer look permanently stale.
  void receivePlaybackSnapshot(const PlaybackInfo & info);

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

  // Single, shared home for "mutate this track's mute/solo/send and keep
  // the already-running playback state in sync" - neither the terminal's
  // `\` key handler nor any Launchpad control (the two ways a user can
  // trigger these today) duplicate this logic; both just resolve which
  // track_id to act on (whichever way is natural for that input source -
  // the shared on-screen cursor, or a Launchpad device's own assigned
  // track) and call these. Returns false (Send setters: no-op) if track_id
  // doesn't name an existing InstrumentTrack/PercussionTrack. Each also
  // pushes the matching PlaybackControlEvent so the change actually reaches
  // the running SongState, not just the Track model - see
  // InstrumentTrackState's public setMuted/setSolo/setSendA/setSendB/
  // setSendMain.
  bool toggleTrackMuted(int track_id);
  bool toggleTrackSolo(int track_id);

  // value is in dB (a perceptual/log scale, easier to dial a subtle send
  // with than a linear fraction) - -100 or below is a hard "off", matching
  // the same floor InstrumentTrack.cpp's XML load/save uses. Converted to
  // the linear multiplier InstrumentTrack/SendLevels.h actually store right
  // here, before either the model or the PlaybackControlEvent ever see it.
  void setTrackSendA(int track_id, float value);
  void setTrackSendB(int track_id, float value);
  void setTrackSendMain(int track_id, float value);
  void setTrackAzimuth(int track_id, float value);

  // Note columns (chord/polyphony width, VisibleTrackInfo::num_subtracks_)
  // are otherwise purely derived from actual note data (see Pattern::
  // getTrackInformation()) - these two adjust InstrumentTrack's own
  // minNoteColumns floor that derivation also takes the max against, a
  // Renoise-style manual override so an empty column can be added ahead of
  // typing into it. No PlaybackControlEvent (unlike the setters above):
  // this only affects display/editing, never audio - the running SongState
  // never reads it.
  void addNoteColumn(int track_id);
  void removeNoteColumn(int track_id);

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

  // Engages the realtime auto-play-while-held session (PatternEditor's
  // keyboard entry and LaunchpadManager's pad entry both offer this):
  // starts the transport and mutes the song's own pattern-driven
  // scheduling (SongState::render()'s own comment has the full reasoning)
  // so only this live take's own PLAY_NOTE/STOP_NOTE/NOTE_PRESSURE stream
  // sounds, then resets the caller's whole-row-replace bookkeeping for the
  // fresh session. The caller decides *when* to call this - its own "is
  // this the first held note, and are we not already playing" check
  // (again per-input-source state, not shareable) - so it's only ever
  // called once per session, right before that session's first write.
  void startAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int & last_cleared_pattern_idx);

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
  void stopAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, const PlaybackInfo & info);

  // Writes an explicit note-off at `row` for a live take's release, once
  // the transport has moved past the note's own row - shared tail of
  // PatternEditor::offerInput()'s and LaunchpadManager::handlePadEvent()'s
  // RELEASE handling (mirroring handleMidiEvent()'s own NOTE_OFF write).
  // Per Renoise's own pattern model, a single line can't hold both a note
  // and its own note-off, so the caller only calls this once it's
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

  const InstrumentProvider & getInstrumentProvider() const { return instrument_provider; }

 private:
  ChannelConfiguration channel_config;
  MixerType mixer_type_ = MixerType::AMBISONIC_STEREO;
  bool use_legacy_binaural_ = false;

  // Every open song, keyed by buffer name - real storage for every buffer
  // openSong()/switchToBuffer() have added and killActiveBuffer() hasn't
  // closed yet, not just the active one (see active_buffer_name_ below).
  // Named songs_, not buffers_: every open buffer is a Song right now, and
  // this map's value type is literally Song - if a future, non-Song
  // buffer kind shows up, that's the point this stops being accurate and
  // needs revisiting, not before.
  std::map<std::string, std::shared_ptr<Song>> songs_;
  // hasUnsavedChanges()'s baseline, one per songs_ entry rather than one
  // shared scalar - each buffer's own unsaved-changes state is independent
  // of whichever buffer happens to be active, so switching the active one
  // must never reset or conflate them. Kept in lockstep with songs_ (same
  // key added/removed/renamed together) by addBuffer()/renameActiveBuffer()/
  // killActiveBuffer() below.
  std::map<std::string, int> last_saved_versions_;
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
  // last_saved_versions_[name] - always song->getVersion() right after a
  // fresh open, but the caller's job to compute since it must be read
  // before this call, not after.
  void addBuffer(std::shared_ptr<Song> song, const std::string & name, int saved_version);
  // Renames the active buffer's own songs_/last_saved_versions_ entry to
  // `new_name` (erase old key, insert new, same Song and active either
  // way) - saveSongAs()'s own tail, once it's already called Song::save().
  // `saved_version` is again the caller's job to compute (song->getVersion()
  // right after that save() call) for the same reason addBuffer() takes it
  // as a parameter rather than reading it itself.
  void renameActiveBuffer(const std::string & new_name, int saved_version);
  // Saves the *currently* active buffer's own live playback_info/
  // recording_track_id/pattern_selection_active_ into their map slots -
  // a no-op before any buffer has ever been active, at startup. Called
  // right *before* a caller (addBuffer()/switchToBuffer()/
  // killActiveBuffer() below) reassigns active_buffer_name_ itself under
  // song_mutex_ - kept as its own step rather than folded into a single
  // "set the active buffer" method so it never needs to take that lock
  // itself (these three scalars are UI-thread-only, untouched by the
  // audio thread, so they don't need it - but calling in from inside a
  // caller's own already-held lock_guard would deadlock on a plain,
  // non-recursive std::mutex).
  void saveActiveBufferState();
  // The other half of saveActiveBufferState(): loads `name`'s own map
  // slot into the three live scalars - each defaults freshly the first
  // time any given buffer name is switched to, via plain
  // std::map::operator[] auto-inserting a default-constructed value, so
  // there's no separate "is this a first visit" case to handle. Called
  // right *after* active_buffer_name_ has already been reassigned to
  // `name` (so `name` here is expected to equal it).
  void loadActiveBufferState(const std::string & name);
  // Drops `name`'s own playback_info/recording_track_id/
  // pattern_selection_active_ map slot entirely - killActiveBuffer()'s own
  // tail (nothing worth keeping for a buffer that's gone) and
  // renameActiveBuffer()'s (the live scalars stay authoritative through a
  // mere rename, untouched by save/loadActiveBufferState(); the *old*
  // key's slot would just be stale dead weight otherwise).
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
  // playback_info/recording_track_id/pattern_selection_active_ below are
  // each a live mirror of whichever buffer is currently active; these
  // three maps (mirroring last_saved_versions_' own shape, keyed the same
  // way) hold every *other* open buffer's own saved copy. save/
  // loadActiveBufferState() are the one place that swap between a live
  // scalar and its map slot.
  std::map<std::string, PlaybackInfo> playback_infos_;
  std::map<std::string, int> recording_track_ids_;
  std::map<std::string, bool> pattern_selection_actives_;
  PlaybackInfo playback_info;
  // How many position-editing control events moveEditPosition()/
  // setEditPosition() have themselves pushed - compared against each
  // incoming snapshot's own PlaybackInfo::getPositionEditSeq() by
  // setPlaybackInfo() to detect a stale one. See that method's own
  // comment. Deliberately global, not per-buffer like the three maps
  // above - see receivePlaybackSnapshot()'s own comment for why.
  int local_position_edit_seq_ = 0;
  int recording_track_id = 0;
  // Controller's own named commands ("save-song", ...) - sendCommand()
  // tries this first, then command_fallback_; commandCompletions() reads
  // its prefix index the same way.
  CommandRegistry commands_;
  std::function<bool(std::string_view)> command_fallback_;
  std::function<std::set<std::string>(std::string_view)> command_completer_;
  std::function<void()> buffer_change_listener_;
  int pending_command_track_ = -1;
  bool pattern_selection_active_ = false;

  static inline AudioBuffer empty_sample;
};

#endif
