#include "Controller.h"

#include "model/Song.h"
#include "model/InstrumentTrack.h"
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
// convention model/InstrumentTrack.cpp's own dbToLinear() (and
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
Controller::addBuffer(std::shared_ptr<Song> song, const string & name, int saved_version) {
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    last_saved_versions_[name] = saved_version;
    songs_[name] = std::move(song);
    active_buffer_name_ = name;
  }
  refreshBufferCommands();
  if (buffer_change_listener_) buffer_change_listener_();
}

void
Controller::renameActiveBuffer(const string & new_name, int saved_version) {
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto song = songs_.at(active_buffer_name_);
    songs_.erase(active_buffer_name_);
    last_saved_versions_.erase(active_buffer_name_);
    songs_[new_name] = std::move(song);
    last_saved_versions_[new_name] = saved_version;
    active_buffer_name_ = new_name;
  }
  refreshBufferCommands();
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
  std::set<std::string> current;
  for (auto & [name, song] : songs_) current.insert(name);
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

  auto version = song->getVersion();
  addBuffer(song, filename, version);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
  return true;
}

bool
Controller::hasUnsavedChanges() const {
  auto song = getCurrentSong();
  if (!song) return false;
  auto it = last_saved_versions_.find(active_buffer_name_);
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
  bool created = false;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    if (songs_.find(name) == songs_.end()) {
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
  if (created) refreshBufferCommands();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
  if (buffer_change_listener_) buffer_change_listener_();
}

void
Controller::cycleBuffer(bool forward) {
  if (songs_.size() < 2) return; // nothing else to switch to
  auto it = songs_.find(active_buffer_name_);
  if (it == songs_.end()) return; // active_buffer_name_ should always be a real key; defensive only
  if (forward) {
    ++it;
    if (it == songs_.end()) it = songs_.begin();
  } else {
    if (it == songs_.begin()) it = songs_.end();
    --it;
  }
  switchToBuffer(it->first);
}

string
Controller::getDefaultSwitchTarget() const {
  if (songs_.size() < 2) return "";
  auto it = songs_.find(active_buffer_name_);
  if (it == songs_.end()) return ""; // defensive only, see cycleBuffer()'s own comment
  ++it;
  if (it == songs_.end()) it = songs_.begin();
  return it->first;
}

bool
Controller::killActiveBuffer() {
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    if (songs_.size() <= 1) return false; // always keep at least one buffer open
    songs_.erase(active_buffer_name_);
    last_saved_versions_.erase(active_buffer_name_);
    active_buffer_name_ = songs_.begin()->first; // name-sorted first remaining buffer
  }
  refreshBufferCommands();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
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
  auto info = getPlaybackInfo();
  info.setIsPlaying(!info.isPlaying());
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(info.isPlaying() ? PlaybackControlEvent::PLAY : PlaybackControlEvent::STOP));
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
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, delta_rows, pattern_selection_active_ ? 1 : 0));
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
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_POSITION, absolute_row, pattern_selection_active_ ? 1 : 0));
}

void
Controller::receivePlaybackSnapshot(const PlaybackInfo & info) {
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

static InstrumentTrack *
asInstrumentTrack(Track * track) {
  if (!track || (track->getType() != TrackType::INSTRUMENT_CONTROL && track->getType() != TrackType::PERCUSSION_CONTROL && track->getType() != TrackType::DRUM_MACHINE)) return nullptr;
  return &dynamic_cast<InstrumentTrack&>(*track);
}

bool
Controller::toggleTrackMuted(int track_id) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return false;
  instrument_track->setMuted(!instrument_track->isMuted());
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MUTED, track_id, instrument_track->isMuted() ? 1 : 0));
  return instrument_track->isMuted();
}

bool
Controller::toggleTrackSolo(int track_id) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return false;
  instrument_track->setSolo(!instrument_track->isSolo());
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SOLO, track_id, instrument_track->isSolo() ? 1 : 0));
  return instrument_track->isSolo();
}

void
Controller::setTrackSendA(int track_id, float value) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  float linear = dbToLinear(value);
  instrument_track->setSendA(linear);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_A, track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendB(int track_id, float value) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  float linear = dbToLinear(value);
  instrument_track->setSendB(linear);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_B, track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendMain(int track_id, float value) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  float linear = dbToLinear(value);
  instrument_track->setSendMain(linear);
  song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_MAIN, track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackAzimuth(int track_id, float value) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setAzimuth(value);
  song->incVersion();
  // Tenths-of-a-degree precision (-1800..1800) - the same "float via a
  // fixed-point int parameter" convention setTrackSendA/B use, just a
  // different scale/unit since this is degrees, not a 0-1 fraction.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_AZIMUTH, track_id, static_cast<int>(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f))));
}

void
Controller::addNoteColumn(int track_id) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setMinNoteColumns(instrument_track->getMinNoteColumns() + 1);
  song->incVersion();
}

void
Controller::removeNoteColumn(int track_id) {
  auto song = getCurrentSong();
  auto instrument_track = asInstrumentTrack(song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setMinNoteColumns(instrument_track->getMinNoteColumns() - 1);
  song->incVersion();
}

void
Controller::ensureRowCleared(std::set<std::pair<int, int>> & cleared_rows, int pattern_idx, int row, int track_id) {
  if (!cleared_rows.insert({row, track_id}).second) return; // already cleared this session
  auto song = getCurrentSong();
  auto & scene = song->getScene(pattern_idx);
  scene.setNotes(row, track_id, {});
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
Controller::startAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int & last_cleared_pattern_idx) {
  togglePlaying();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, 1));
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
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, 0));
  auto_started_playback = false;
  cleared_rows.clear(); // not required for correctness (the next session's own start resets this too) - just don't hold onto a finished session's bookkeeping longer than needed
}

void
Controller::writeReleaseOff(std::set<std::pair<int, int>> & cleared_rows, bool auto_started_playback, int pattern_idx, int row, int track_id, int note_column, int delay) {
  if (auto_started_playback) ensureRowCleared(cleared_rows, pattern_idx, row, track_id);
  auto song = getCurrentSong();
  auto & scene = song->getScene(pattern_idx);
  scene.setNote(row, track_id, note_column, Note(0, 0, delay));
  song->incVersion();
}

void
Controller::applyNotePressure(int pattern_idx, int row, int track_id, int note_column, short velocity, int delay) {
  auto song = getCurrentSong();
  auto & scene = song->getScene(pattern_idx);
  auto note = scene.getNote(row, track_id, note_column);
  if (!note.isDefined()) note.setDelay(delay);
  note.setVelocity(velocity);
  scene.setNote(row, track_id, note_column, note);
}
