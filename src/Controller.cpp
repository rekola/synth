#include "Controller.h"

#include "model/Song.h"
#include "model/LeafTrack.h"
#include "model/InstrumentTrack.h"
#include "model/ArrangementOps.h"
#include "playback/PlaybackControlEvent.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>

#include <fmt/core.h>

using namespace std;

namespace {

// Self-contained (not TreeNode::decibelsToGain(), only reachable from
// TreeNode<Derived> subclasses - VoiceState/TrackState, neither of which
// Controller is) - the same "each file keeps its own small dB helper"
// convention model/LeafTrack.cpp's own dbToLinear() (and
// effects/Compressor.cpp's db2lin(), dsp/TapeTransport.cpp's/
// effects/TapeDegradation.cpp's own dbToLinear()) already use, including
// the same -100dB "off" floor.
float dbToLinear(float db) { return db > -100.0f ? powf(10.0f, db * 0.05f) : 0.0f; }

// Splits a '/'-delimited path string into its components, in order -
// uniqueDisplayName() below grows its disambiguating suffix one component
// at a time. Plain string splitting rather than std::filesystem::path
// iteration: an absolute path's leading "/" shows up as its own component
// under path iteration, which would need special-casing here for no
// benefit - buffer names are only ever compared against each other, never
// against the real filesystem.
vector<string> splitPathComponents(const string & path) {
  vector<string> parts;
  size_t start = 0;
  while (true) {
    auto pos = path.find('/', start);
    if (pos == string::npos) {
      parts.push_back(path.substr(start));
      break;
    }
    parts.push_back(path.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

// Joins the last `count` elements of `parts` (or all of them, if `count`
// exceeds how many there are) back into a '/'-delimited string - the
// growing disambiguating suffix uniqueDisplayName() compares.
string joinLastComponents(const vector<string> & parts, size_t count) {
  size_t start = count >= parts.size() ? 0 : parts.size() - count;
  string result;
  for (size_t i = start; i < parts.size(); i++) {
    if (i > start) result += "/";
    result += parts[i];
  }
  return result;
}

// Emacs-style uniquify, as a pure function of names alone - see
// Controller::getBufferDisplayName()'s own doc comment on Controller.h for
// what it's for and where it's shown. `all_names` is every currently open
// buffer, `name` (harmlessly skipped when comparing against itself) among
// them.
string uniqueDisplayName(const string & name, const vector<string> & all_names) {
  namespace fs = std::filesystem;
  auto basename = fs::path(name).filename().string();

  // Every other open buffer sharing this one's basename, each with its
  // own basename already dropped (it's known identical to `name`'s across
  // the whole group - nothing to gain comparing it again) down to just
  // its directory components.
  vector<vector<string>> colliding_dir_parts;
  for (auto & other : all_names) {
    if (other == name) continue;
    if (fs::path(other).filename() != basename) continue;
    auto parts = splitPathComponents(other);
    if (!parts.empty()) parts.pop_back();
    colliding_dir_parts.push_back(std::move(parts));
  }
  if (colliding_dir_parts.empty()) return basename;

  auto name_dir_parts = splitPathComponents(name);
  if (!name_dir_parts.empty()) name_dir_parts.pop_back();

  // How far a directory suffix could possibly need to grow before ruling
  // out every collision - not just `name`'s own depth: a shorter path
  // colliding against a longer one needs to walk out as far as the
  // *longer* one goes, or two colliding paths of different lengths that
  // happen to share every one of the shorter one's own components as a
  // trailing run (e.g. "a/b/song.xml" vs "x/a/b/song.xml") would never
  // resolve within the shorter path's own bound.
  size_t max_depth = name_dir_parts.size();
  for (auto & parts : colliding_dir_parts) max_depth = std::max(max_depth, parts.size());

  for (size_t depth = 1; depth <= max_depth; depth++) {
    auto suffix = joinLastComponents(name_dir_parts, depth);
    bool unique = true;
    for (auto & parts : colliding_dir_parts) {
      if (joinLastComponents(parts, depth) == suffix) { unique = false; break; }
    }
    if (unique) return basename + "<" + suffix + ">";
  }
  // Every directory component on both sides exhausted and still tied -
  // only possible if two *different* songs_ keys produced identical
  // name/dir splits, which can't happen (songs_'s own keys are unique
  // strings) - the full name is always unambiguous as a last resort.
  return name;
}

}

// Find a General MIDI SoundFont. Priority: the project-local data/ override,
// then well-known GM fonts by name (user dirs before system dirs;
// default-GM.sf2 is the Ubuntu alternatives-managed default), and finally the
// largest .sf2 found anywhere in the searched directories.
static string
findDefaultSoundFont() {
  namespace fs = std::filesystem;
  error_code ec;

  if (fs::is_regular_file("data/FluidR3_GM.sf2", ec)) return "data/FluidR3_GM.sf2";

  vector<fs::path> dirs;
  if (auto home = getenv("HOME")) {
    dirs.push_back(fs::path(home) / ".local/share/soundfonts");
    dirs.push_back(fs::path(home) / ".local/share/sounds/sf2");
  }
  dirs.push_back("/usr/share/soundfonts");
  dirs.push_back("/usr/share/sounds/sf2");

  const char * preferred[] = {
    "FluidR3_GM.sf2",
    "default-GM.sf2",
    "MuseScore_General.sf2",
    "GeneralUser GS.sf2",
    "TimGM6mb.sf2",
  };
  for (auto name : preferred) {
    for (auto & dir : dirs) {
      auto p = dir / name;
      if (fs::is_regular_file(p, ec)) return p.string();
    }
  }

  fs::path best;
  uintmax_t best_size = 0;
  for (auto & dir : dirs) {
    for (auto & entry : fs::directory_iterator(dir, ec)) {
      if (entry.path().extension() != ".sf2") continue;
      auto size = fs::file_size(entry.path(), ec);
      if (!ec && size > best_size) {
	best_size = size;
	best = entry.path();
      }
    }
  }
  return best.string();
}

Controller::Controller(ChannelConfiguration _channel_config) : channel_config(_channel_config) {
  auto soundfont = findDefaultSoundFont();
  if (!soundfont.empty()) {
    fmt::print(stderr, "Using SoundFont {}\n", soundfont);
    instrument_provider.loadSoundFont(soundfont);
  } else {
    fmt::print(stderr, "No GM SoundFont found; only built-in instruments available\n");
  }
  error_code ec;
  if (std::filesystem::is_regular_file("data/Essential Keys-sforzando-v9.6.sf2", ec)) {
    instrument_provider.loadSoundFont("data/Essential Keys-sforzando-v9.6.sf2", false);
  }

  // MixerFactory falls back to AMBISONIC_STEREO at actual mixer-
  // construction time if no SOFA file resolves (or libmysofa isn't
  // compiled in), so defaulting to AMBISONIC_BINAURAL here is safe even
  // when that fallback will immediately kick in - and harmless for a MONO
  // config too, since MixerFactory never attempts binaural for MONO
  // regardless of this setting.
#ifdef SYNTH_HAVE_LIBMYSOFA
  mixer_type_ = MixerType::AMBISONIC_BINAURAL;
#else
  mixer_type_ = MixerType::AMBISONIC_STEREO;
#endif

  commands_.define("save-song", [this]() {
    auto song = getCurrentSong();
    song->save(getActiveBufferName());
    last_saved_versions_[getActiveBufferName()] = song->getVersion();
  });
  commands_.define("add-filter", [this]() { });
  // Placeholder stubs (menu-visible, TerminalMenu's Song section) for
  // Song::getKey()/setKey() and getTuning()/setTuning(), which already
  // exist on the model but have no UI path to reach them yet - a prompt
  // for the key/tuning value itself (e.g. picking among Tuning's 12/19/
  // 31/53-EDO values) is separate, not-yet-scheduled work.
  commands_.define("set-song-key", [this]() { });
  commands_.define("set-song-tuning", [this]() { });
  commands_.define("toggle-mixer-type", [this]() {
    // "Bypass HRTF entirely" toggle: AMBISONIC_STEREO <-> AMBISONIC_BINAURAL.
    // A no-op for a MONO config - MixerFactory never attempts binaural
    // decoding there regardless of this setting (see MixerFactory.cpp) -
    // but harmless to still flip, so no type check is needed here either.
    mixer_type_ = (mixer_type_ == MixerType::AMBISONIC_BINAURAL) ? MixerType::AMBISONIC_STEREO : MixerType::AMBISONIC_BINAURAL;
    fmt::print(stderr, "Mixer type set to {}\n", to_string(mixer_type_));
    getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MIXER_CHANGED));
  });
}

void
Controller::saveActiveBufferState() {
  if (active_buffer_name_.empty()) return; // startup - no buffer has ever been active yet
  // canonicalBufferName(): a session-view alias and its own canonical
  // buffer share one live-state slot - see session_view_aliases_'s own
  // comment.
  auto key = canonicalBufferName(active_buffer_name_);
  playback_infos_[key] = playback_info;
  recording_track_ids_[key] = recording_track_id;
  pattern_selection_actives_[key] = pattern_selection_active_;
  local_position_edit_seqs_[key] = local_position_edit_seq_;
  focused_clip_ids_[key] = focused_clip_id_;
  focused_clip_track_ids_[key] = focused_clip_track_id_;
}

void
Controller::loadActiveBufferState(const string & name) {
  auto key = canonicalBufferName(name);
  playback_info = playback_infos_[key];
  recording_track_id = recording_track_ids_[key];
  pattern_selection_active_ = pattern_selection_actives_[key];
  local_position_edit_seq_ = local_position_edit_seqs_[key];
  focused_clip_id_ = focused_clip_ids_[key];
  // Not operator[] like the scalars above - int's own default-constructed
  // value (0) would misread as "focused on track 0" for a buffer that's
  // never had a focus at all, rather than "no focus" (-1).
  auto track_it = focused_clip_track_ids_.find(key);
  focused_clip_track_id_ = track_it == focused_clip_track_ids_.end() ? -1 : track_it->second;
}

void
Controller::dropBufferState(const string & name) {
  playback_infos_.erase(name);
  recording_track_ids_.erase(name);
  pattern_selection_actives_.erase(name);
  local_position_edit_seqs_.erase(name);
  focused_clip_ids_.erase(name);
  focused_clip_track_ids_.erase(name);
}

void
Controller::stopFocusedClipPreview() {
  if (focused_clip_track_id_ < 0) return;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, getActiveBufferName(), focused_clip_track_id_));
}

void
Controller::addBuffer(std::shared_ptr<Song> song, const string & name, Version saved_version) {
  saveActiveBufferState();
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    last_saved_versions_[name] = saved_version;
    songs_[name] = std::move(song);
    active_buffer_name_ = name;
  }
  loadActiveBufferState(name);
  refreshBufferCommands();
  if (buffer_change_listener_) buffer_change_listener_();
}

void
Controller::renameActiveBuffer(const string & new_name, Version saved_version) {
  // A rename, not a switch - the active buffer itself doesn't change, so
  // the live playback_info/recording_track_id/pattern_selection_active_
  // scalars stay exactly as they are; only the *old* key's own map slot
  // (if any, from a previous session under a different name) needs
  // dropping so it doesn't linger as dead weight under a name nothing
  // will ever look up again. Resolved to canonical first - the active
  // buffer here is always meant to be the real Song (Save As has no
  // separate meaning for a session-view alias), even if the alias itself
  // happens to be what's currently selected.
  auto old_name = canonicalBufferName(active_buffer_name_);
  dropBufferState(old_name);
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto song = songs_.at(old_name);
    songs_.erase(old_name);
    last_saved_versions_.erase(old_name);
    songs_[new_name] = std::move(song);
    last_saved_versions_[new_name] = saved_version;
    // Every alias of the renamed buffer follows it - its own alias name
    // doesn't change, only which canonical buffer it resolves to, so a
    // session view stays attached to "the same song" through a rename
    // rather than dangling.
    for (auto & [alias, canonical] : session_view_aliases_) {
      if (canonical == old_name) canonical = new_name;
    }
    active_buffer_name_ = (active_buffer_name_ == old_name) ? new_name : active_buffer_name_;
  }
  refreshBufferCommands();
  // Rekeys the renamed buffer's own live SongState (if any) rather than
  // dropping and lazily recreating it under the new name - a still-
  // sounding voice/release tail must survive a rename exactly like it
  // survives any other buffer switch (see the per-buffer editing/
  // playback-state plan's Part B).
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::BUFFER_RENAMED, old_name, new_name));
  if (buffer_change_listener_) buffer_change_listener_();
}

// Keeps one CommandRegistry command per open buffer in sync with songs_'s
// own keys - "switch-to-buffer:<name>", each switching straight to that
// exact (not display-shortened) buffer. This is the one thing a shared/
// generic command name can't parameterize: a Buffers-menu item's own
// label is only ever a basename (TerminalMenu::rebuild()), so without a
// real per-buffer command the menu would have no way to tell two
// same-basename buffers apart when clicked. Removes the entries for any
// buffer that closed/renamed since the last call (CommandRegistry::
// undefine(), or a stale entry would still be both executable and
// M-x-completable, and worse - since switchToBuffer() creates a buffer it
// doesn't recognize rather than failing, a stale command would silently
// resurrect a dead buffer name instead of just doing nothing).
void
Controller::refreshBufferCommands() {
  // getBufferNames()' own merge (real buffers plus session-view aliases) -
  // an alias needs a "switch-to-buffer:<name>" entry exactly like a real
  // buffer does, for the Buffers-menu click path (see this method's own
  // doc comment on Controller.h).
  std::set<std::string> current;
  for (auto & name : getBufferNames()) current.insert(name);
  for (auto & name : registered_buffer_commands_) {
    if (!current.count(name)) commands_.undefine("switch-to-buffer:" + name);
  }
  for (auto & name : current) {
    commands_.define("switch-to-buffer:" + name, [this, name]() { switchToBuffer(name); });
  }
  registered_buffer_commands_ = std::move(current);
}

string
Controller::freshBufferName() const {
  if (songs_.find("song.xml") == songs_.end()) return "song.xml";
  for (int i = 2; ; i++) {
    auto candidate = "song-" + std::to_string(i) + ".xml";
    if (songs_.find(candidate) == songs_.end()) return candidate;
  }
}

string
Controller::getBufferDisplayName(const string & name) const {
  return uniqueDisplayName(name, getBufferNames());
}

bool
Controller::openSong(const string & filename) {
  if (songs_.find(filename) != songs_.end()) {
    // Already open - switch to it rather than re-reading the file (which
    // would silently discard any in-memory edits the open copy has that
    // the file itself doesn't). Not switchToBuffer(): that creates a
    // fresh blank buffer for a name it doesn't recognize, which is right
    // for select-named-buffer/"New" but wrong here - a missing file
    // should fail, not silently open a blank song under its name.
    switchToBuffer(filename);
    return true;
  }

  auto song = make_shared<Song>();
  if (!song->open(filename, instrument_provider)) {
    return false;
  }

  addBuffer(song, filename, song->getVersion());
  return true;
}

bool
Controller::hasUnsavedChanges() const {
  auto song = getCurrentSong();
  if (!song) return false;
  auto it = last_saved_versions_.find(canonicalBufferName(active_buffer_name_));
  return it == last_saved_versions_.end() || song->getVersion() != it->second;
}

bool
Controller::hasAnyUnsavedChanges() const {
  for (auto & [name, song] : songs_) {
    auto it = last_saved_versions_.find(name);
    if (it == last_saved_versions_.end() || song->getVersion() != it->second) return true;
  }
  return false;
}

void
Controller::saveSongAs(const string & filename) {
  auto song = getCurrentSong();
  song->save(filename);
  renameActiveBuffer(filename, song->getVersion());
}

void
Controller::switchToBuffer(const string & name) {
  saveActiveBufferState();
  bool created = false;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    // Existence is checked against the canonical name - a session-view
    // alias already open (session_view_aliases_) always resolves to a
    // real songs_ entry, so this correctly takes the "already open" path
    // for it without ever creating a fresh Song under the alias's own
    // name (an alias is only ever created by openSessionViewBuffer()
    // against an already-open buffer to begin with).
    if (songs_.find(canonicalBufferName(name)) == songs_.end()) {
      // Not open yet - create it fresh, same starter content "New" used
      // to set up back when it was its own command (see this method's own
      // doc comment on Controller.h).
      auto song = make_shared<Song>();
      song->addTrack(make_unique<InstrumentTrack>(0));
      song->addScene();
      last_saved_versions_[name] = song->getVersion();
      songs_[name] = std::move(song);
      created = true;
    }
    active_buffer_name_ = name;
  }
  loadActiveBufferState(name);
  if (created) refreshBufferCommands();
  if (buffer_change_listener_) buffer_change_listener_();
}

string
Controller::openSessionViewBuffer() {
  auto canonical = canonicalBufferName(active_buffer_name_);
  auto alias = canonical + " [Session]";
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    session_view_aliases_[alias] = canonical; // idempotent if already open
  }
  // switchToBuffer()'s own `created` flag never fires for an alias (it's
  // never inserted into songs_ itself - see that method's own comment),
  // so this call is what actually registers the alias's own
  // "switch-to-buffer:" entry/Buffers-menu row the first time.
  refreshBufferCommands();
  switchToBuffer(alias);
  return alias;
}

void
Controller::cycleBuffer(bool forward) {
  // Over every buffer-list entry (real buffers plus session-view aliases,
  // getBufferNames()' own merge), not just songs_'s own keys - Next/
  // Previous-buffer should visit an alias too, the same as any other
  // buffer-list entry.
  auto names = getBufferNames();
  if (names.size() < 2) return; // nothing else to switch to
  auto it = std::find(names.begin(), names.end(), active_buffer_name_);
  if (it == names.end()) return; // active_buffer_name_ should always be a real entry; defensive only
  if (forward) {
    ++it;
    if (it == names.end()) it = names.begin();
  } else {
    if (it == names.begin()) it = names.end();
    --it;
  }
  switchToBuffer(*it);
}

string
Controller::getDefaultSwitchTarget() const {
  auto names = getBufferNames();
  if (names.size() < 2) return "";
  auto it = std::find(names.begin(), names.end(), active_buffer_name_);
  if (it == names.end()) return ""; // defensive only, see cycleBuffer()'s own comment
  ++it;
  if (it == names.end()) it = names.begin();
  return *it;
}

bool
Controller::killActiveBuffer() {
  // Closing a session-view alias just drops the alias entry itself and
  // returns to its own canonical buffer - the underlying Song/its other
  // state (last_saved_versions_, playback/edit state, ...) is untouched,
  // since it's still open under its own real name. Unlike the canonical-
  // kill path below, this never refuses (there's always somewhere to
  // return to: the canonical buffer it was aliasing).
  string alias_canonical;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto it = session_view_aliases_.find(active_buffer_name_);
    if (it != session_view_aliases_.end()) {
      alias_canonical = it->second;
      session_view_aliases_.erase(it);
    }
  }
  if (!alias_canonical.empty()) {
    refreshBufferCommands(); // drops the now-gone alias's own "switch-to-buffer:" entry
    switchToBuffer(alias_canonical);
    return true;
  }

  string killed_name;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    if (songs_.size() <= 1) return false; // always keep at least one buffer open
    killed_name = active_buffer_name_;
    songs_.erase(active_buffer_name_);
    last_saved_versions_.erase(active_buffer_name_);
    // Drops any session-view alias that pointed at the now-gone buffer -
    // a dangling alias would otherwise still list/switch-to-buffer into a
    // canonical name with no real Song behind it any more.
    for (auto it = session_view_aliases_.begin(); it != session_view_aliases_.end(); ) {
      if (it->second == killed_name) it = session_view_aliases_.erase(it); else ++it;
    }
    active_buffer_name_ = songs_.begin()->first; // name-sorted first remaining buffer
  }
  // The killed buffer's own saved state (if any) is discarded along with
  // it, not preserved anywhere - nothing left to switch back to it for.
  dropBufferState(killed_name);
  loadActiveBufferState(active_buffer_name_);
  refreshBufferCommands();
  // Drops the killed buffer's own live SongState, if it had one (Player::
  // handlePlaybackControlEvent()) - also stops it automatically if it
  // happened to be the playing buffer, simply by no longer existing to
  // render at all, rather than needing a separate STOP first.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::BUFFER_KILLED, killed_name));
  if (buffer_change_listener_) buffer_change_listener_();
  return true;
}

bool
Controller::sendCommand(std::string_view cmd) {
  if (commands_.execute(std::string(cmd))) return true;
  if (command_fallback_) return command_fallback_(cmd);
  return false;
}

std::set<std::string>
Controller::commandCompletions(std::string_view prefix) const {
  auto & own = commands_.matching(std::string(prefix));
  std::set<std::string> result(own.begin(), own.end());
  if (command_completer_) {
    auto more = command_completer_(prefix);
    result.insert(more.begin(), more.end());
  }
  return result;
}

bool
Controller::togglePlaying() {
  // Always targets the *active* buffer - pressing Space always means "play/
  // stop the buffer I'm looking at right now", taking over the single
  // "playing" role from whatever else was playing if that's a different
  // buffer (Player::handlePlaybackControlEvent()'s PLAY case demotes
  // whatever the previous playing_buffer_name_ was to audition-only,
  // rather than tearing it down - see the per-buffer editing/playback-
  // state plan's Part B).
  auto info = getPlaybackInfo();
  info.setIsPlaying(!info.isPlaying());
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(info.isPlaying() ? PlaybackControlEvent::PLAY : PlaybackControlEvent::STOP, getActiveBufferName()));
  setPlaybackInfo(info);
  return info.isPlaying();
}

void
Controller::moveEditPosition(int delta_rows) {
  auto info = getPlaybackInfo();
  // Row navigation only ever runs while stopped (see PatternEditor's own
  // "Row navigation while stopped" comment). clampRowToCurrentPattern()
  // only applies while pattern_selection_active_ - see its own comment -
  // so a mark set for a selection can never end up stranded in a
  // different pattern than the cursor, but plain navigation with no
  // selection open crosses pattern boundaries freely. Floor at 0
  // otherwise (matching SongState::movePosition()'s own floor) - there's
  // no upper bound either way, same as real playback's own row-by-row
  // advance: running off the end of the last pattern is normal, not
  // special-cased. Player::handlePlaybackControlEvent() applies the
  // identical decision on the audio-thread side to the MOVE_POSITION
  // event this pushes below, rather than trusting a value computed by
  // the UI thread across the thread boundary - parameter2 carries
  // whether to clamp, so both sides make the same choice.
  auto new_absolute = pattern_selection_active_ ?
    getSong().clampRowToCurrentPattern(info.getAbsolutePosition(), info.getAbsolutePosition() + delta_rows) :
    max(0, info.getAbsolutePosition() + delta_rows);
  auto [ pattern_idx, row_idx ] = getSong().normalizePosition(0, new_absolute);
  info.setAbsolutePos(new_absolute);
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setPositionEditSeq(++local_position_edit_seq_);
  setPlaybackInfo(info);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, getActiveBufferName(), delta_rows, pattern_selection_active_ ? 1 : 0));
}

void
Controller::setEditPosition(int absolute_row) {
  auto info = getPlaybackInfo();
  // See moveEditPosition()'s own comment - same clamp-only-with-an-open-
  // selection rule, same parameter2-carries-the-decision handshake with
  // the audio thread.
  auto new_absolute = pattern_selection_active_ ?
    getSong().clampRowToCurrentPattern(info.getAbsolutePosition(), absolute_row) : max(0, absolute_row);
  auto [ pattern_idx, row_idx ] = getSong().normalizePosition(0, new_absolute);
  info.setAbsolutePos(new_absolute);
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setPositionEditSeq(++local_position_edit_seq_);
  setPlaybackInfo(info);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_POSITION, getActiveBufferName(), absolute_row, pattern_selection_active_ ? 1 : 0));
}

void
Controller::receivePlaybackSnapshot(const string & buffer_name, const PlaybackInfo & info) {
  // buffer_name always names a real Song (every PlaybackControlEvent is
  // tagged via getActiveBufferName(), always canonical) - compare against
  // the canonical form of whatever's actually selected, so a snapshot for
  // the song currently being looked at still matches while viewing one of
  // its session-view aliases rather than being mistaken for "some other,
  // unrelated buffer playing in the background."
  if (buffer_name != canonicalBufferName(active_buffer_name_)) {
    // Not the buffer currently being looked at/edited - e.g. a buffer
    // still playing in the background while a different one is active
    // (see the per-buffer editing/playback-state plan's Part B).
    // moveEditPosition()/setEditPosition() never run against a buffer
    // that isn't active, so there's no local edit-position prediction
    // that could go stale for it - straight overwrite, same shape a plain
    // setPlaybackInfo() call has for the active buffer below.
    playback_infos_[buffer_name] = info;
    return;
  }
  if (info.getPositionEditSeq() < local_position_edit_seq_) {
    // Stale: the audio thread took this snapshot before draining our most
    // recent moveEditPosition()/setEditPosition() control event. Keep
    // every other field from the real snapshot (voice counts, is_playing,
    // meters, ...) but preserve the local, already-correct edit-position
    // fields rather than regressing them - see this method's own doc
    // comment on Controller.h.
    auto merged = info;
    merged.setAbsolutePos(playback_info.getAbsolutePosition());
    merged.setPatternIdx(playback_info.getPatternIndex());
    merged.setRowIdx(playback_info.getRowIndex());
    merged.setPositionEditSeq(playback_info.getPositionEditSeq());
    setPlaybackInfo(merged);
  } else {
    setPlaybackInfo(info);
  }
}

// A plain dynamic_cast, not a TrackType enumeration - "is this track
// usable as a LeafTrack" (mute/solo/send/azimuth/note-columns - see
// LeafTrack.h) has exactly one answer (its actual C++ type), and
// enumerating TrackTypes here separately risked drifting out of sync with
// it (SampleTrack becoming a leaf track in its own right - see
// SampleTrack.h - is exactly the case that already bit this once).
static LeafTrack *
asLeafTrack(Track * track) {
  return track ? dynamic_cast<LeafTrack *>(track) : nullptr;
}

bool
Controller::toggleTrackMuted(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return false;
  leaf_track->setMuted(!leaf_track->isMuted());
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MUTED, getActiveBufferName(), track_id, leaf_track->isMuted() ? 1 : 0));
  return leaf_track->isMuted();
}

bool
Controller::toggleTrackSolo(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return false;
  leaf_track->setSolo(!leaf_track->isSolo());
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SOLO, getActiveBufferName(), track_id, leaf_track->isSolo() ? 1 : 0));
  return leaf_track->isSolo();
}

bool
Controller::toggleTrackCollapsed(int track_id) {
  auto song = getCurrentSong();
  auto track = song->getMasterTrack().getChildByInternalId(track_id);
  if (!track) return false;
  track->setCollapsed(!track->isCollapsed());
  song->incVersion();
  return track->isCollapsed();
}

void
Controller::setTrackSendA(int track_id, float value) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  float linear = dbToLinear(value);
  leaf_track->setSendA(linear);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_A, getActiveBufferName(), track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendB(int track_id, float value) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  float linear = dbToLinear(value);
  leaf_track->setSendB(linear);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_B, getActiveBufferName(), track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendMain(int track_id, float value) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  float linear = dbToLinear(value);
  leaf_track->setSendMain(linear);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_MAIN, getActiveBufferName(), track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setBusEffectKind(int slot, BusEffectKind kind) {
  auto song = getCurrentSong();
  song->setBusSlotKind(slot, kind);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_BUS_EFFECT, getActiveBufferName(), slot, static_cast<int>(kind)));
}

void
Controller::setTrackAzimuth(int track_id, float value) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  leaf_track->setAzimuth(value);
  song->incVersion();
  // Tenths-of-a-degree precision (-1800..1800) - the same "float via a
  // fixed-point int parameter" convention setTrackSendA/B use, just a
  // different scale/unit since this is degrees, not a 0-1 fraction.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_AZIMUTH, getActiveBufferName(), track_id, static_cast<int>(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f))));
}

void
Controller::addNoteColumn(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  leaf_track->setMinNoteColumns(leaf_track->getMinNoteColumns() + 1);
  song->incVersion();
}

void
Controller::removeNoteColumn(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  leaf_track->setMinNoteColumns(leaf_track->getMinNoteColumns() - 1);
  song->incVersion();
}

void
Controller::ensureRowCleared(std::set<std::pair<int, int>> & cleared_rows, int pattern_idx, int row, int track_id) {
  auto song = getCurrentSong();
  auto & scene = song->getScene(pattern_idx);
  // A shorter-than-song-length Pattern repeats - see Pattern.h's own
  // getEffectiveRow() comment - so the row actually cleared is this
  // track's own effective one, not necessarily the raw playhead row.
  // cleared_rows itself is keyed by that same effective row too, not the
  // raw one: two different raw rows that repeat the same underlying
  // content (e.g. rows 4 and 20 of a 16-row pattern) must dedup together
  // - otherwise sweeping past the second one would clear (and lose) a
  // note the first one just wrote, one loop iteration into the same
  // live take.
  auto effective_row = scene.getEffectiveRow(track_id, row, song->getEffectiveSceneLength(scene));
  if (!cleared_rows.insert({effective_row, track_id}).second) return; // already cleared this session
  scene.setNotes(effective_row, track_id, {});
  song->incVersion();
}

void
Controller::sweepAutoRecordRows(std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int & last_cleared_pattern_idx, int pattern_idx, int new_row, const std::vector<int> & track_ids) {
  if (pattern_idx != last_cleared_pattern_idx || new_row < last_cleared_row) {
    last_cleared_row = new_row - 1;
    last_cleared_pattern_idx = pattern_idx;
  }
  if (new_row <= last_cleared_row) return; // nothing new to sweep

  for (int row = last_cleared_row + 1; row <= new_row; row++) {
    for (auto track_id : track_ids) {
      ensureRowCleared(cleared_rows, pattern_idx, row, track_id);
    }
  }
  last_cleared_row = new_row;
}

void
Controller::extendRecordingSceneIfNeeded(bool recording) {
  if (!recording) return;
  auto & info = getPlaybackInfo();
  if (!info.isPlaying()) return;

  auto song = getCurrentSong();
  if (!song) return;
  auto & scene = song->getScene(info.getPatternIndex());
  auto rows_per_bar = std::max(1, song->getRowsPerBar());
  auto last_bar = std::max(0, scene.getLengthBars() - 1);
  if (info.getRowIndex() / rows_per_bar < last_bar) return; // not near the end yet

  scene.setLengthBars(scene.getLengthBars() + 1);
  song->incVersion();
}

void
Controller::startAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int & last_cleared_pattern_idx) {
  togglePlaying();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, getActiveBufferName(), 1));
  auto_started_playback = true;
  cleared_rows.clear();
  last_cleared_row = -1;
  last_cleared_pattern_idx = -1;
}

void
Controller::stopAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, const PlaybackInfo & info) {
  if (info.isPlaying()) {
    togglePlaying();
    // Land past the just-written final OFF, not directly on it - an
    // absolute SET_POSITION, not a relative MOVE_POSITION(1), since the
    // audio thread keeps advancing in real time for however long this
    // event takes to actually reach it; "+1 from wherever it's drifted to
    // by then" occasionally overshot by an extra row. See SongState::
    // setPosition()'s own comment for the full reasoning.
    setEditPosition(info.getAbsolutePosition() + 1);
  }
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, getActiveBufferName(), 0));
  auto_started_playback = false;
  cleared_rows.clear(); // not required for correctness (the next session's own start resets this too) - just don't hold onto a finished session's bookkeeping longer than needed
}

void
Controller::writeReleaseOff(std::set<std::pair<int, int>> & cleared_rows, bool auto_started_playback, int pattern_idx, int row, int track_id, int note_column, int delay) {
  if (auto_started_playback) ensureRowCleared(cleared_rows, pattern_idx, row, track_id);
  auto song = getCurrentSong();
  auto & scene = song->getOrCreateScene(pattern_idx);
  auto target = resolveEditTarget(*song, scene, track_id, row, getFocusedClip());
  target.pattern->setNote(target.effective_row, note_column, Note(0, 0, delay));
  song->incVersion();
}

void
Controller::applyNotePressure(int pattern_idx, int row, int track_id, int note_column, short velocity, int delay) {
  auto song = getCurrentSong();
  auto & scene = song->getOrCreateScene(pattern_idx);
  auto target = resolveEditTarget(*song, scene, track_id, row, getFocusedClip());
  auto note = target.pattern->getNote(target.effective_row, note_column);
  if (!note.isDefined()) note.setDelay(delay);
  note.setVelocity(velocity);
  target.pattern->setNote(target.effective_row, note_column, note);
}
