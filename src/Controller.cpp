#include "Controller.h"

#include "model/Song.h"
#include "model/TrackType.h"
#include "model/LeafTrack.h"
#include "model/InstrumentTrack.h"
#include "model/PercussionTrack.h"
#include "model/ArrangementOps.h"
#include "model/Clip.h"
#include "instruments/InstrumentLibrary.h"
#include "playback/PlaybackControlEvent.h"
#include "playback/LogEvent.h"

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

  // After every loadSoundFont() call, whether or not one actually found a
  // font: the GM pad overrides need loadSoundFont()'s own registrations to
  // already be in place to take priority over (registerPath()'s last-
  // write-wins), and the additive piano's fallback role needs to see
  // whether a SoundFont piano is already registered - both true regardless
  // of whether any SoundFont was found at all.
  registerLibraryInstruments(instrument_provider);

  // Forces every library PadSynth's middle-register wavetable to build now
  // (startup, main thread) rather than lazily on the real-time audio
  // thread's first render() of it - table generation is ~10-16ms, easily
  // enough to glitch a real-time audio callback; reported in practice as
  // an audible click the first time a Library pad is previewed. Only
  // covers 31-EDO (a fresh song's own default tuning) at a middle
  // register - a song in a different tuning, or a note far from that
  // register, still builds lazily on first use, same as before.
  prewarmLibraryInstruments(instrument_provider, channel_config, Tuning::EDO31);

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
  // Folds a clip's placement back into its track's own background content,
  // freeing the clip slot without deleting the clip itself - a destructive
  // overwrite for a note-based clip, a real additive mix for a SampleTrack
  // one (ArrangementOps.h's own mergeClipToBackground() has the full
  // reasoning). Targets the Song's own current track (Song::
  // getCurrentTrackId() - shared by every buffer viewing this Song, not
  // any one UI widget's own cursor) and the playhead's own row
  // (getPlaybackInfo(), already Controller-global) - resolveInstanceAt()
  // is row-exact, matching kill-row's own resolution. A silent no-op if
  // nothing resolves (no current track set, nothing placed there, or the
  // resolved clip has nothing playable to merge).
  commands_.define("merge-clip-to-background", [this]() {
    auto & song = getSong();
    auto track_id = song.getCurrentTrackId();
    if (track_id < 0) return;
    Song::Edit edit(song, "merge clip to background");
    if (!mergeClipToBackground(song, track_id, playback_info.getAbsolutePosition(), getChannelConfiguration())) {
      edit.discard();
      return;
    }
    getUIEventQueue().push(make_unique<LogEvent>("Clip merged to background"));
  });
  commands_.define("toggle-metronome", [this]() {
    setMetronomeOn(!metronome_on_);
    getUIEventQueue().push(make_unique<LogEvent>(metronome_on_ ? "Metronome on" : "Metronome off"));
  });
  commands_.define("tempo-increase", [this]() { setTempo(getSong().getTempo() + 1); });
  commands_.define("tempo-decrease", [this]() { setTempo(getSong().getTempo() - 1); });
  commands_.define("swing-increase", [this]() { setSwing(getSong().getSwing() + 1); });
  commands_.define("swing-decrease", [this]() { setSwing(getSong().getSwing() - 1); });
  commands_.define("toggle-record-quantize", [this]() {
    auto & song = getSong();
    Song::Edit edit(song, "toggle record quantize");
    song.setRecordQuantize(!song.getRecordQuantize());
    getUIEventQueue().push(make_unique<LogEvent>(song.getRecordQuantize() ? "Record quantise on" : "Record quantise off"));
  });
  // One Record Arm for every track type, reachable from a Launchpad
  // (CC19), a keybinding, or M-x alike.
  //
  // Record Arm is one global state, not a per-track-type toggle -
  // whatever is currently armed/recording (if anything) always takes
  // priority over arming something new, so a single press always means
  // "stop that", regardless of which track happens to be selected right
  // now; changing the selected track while armed is harmless, since
  // disarming never re-reads it.
  commands_.define("toggle-record-arm", [this]() {
    auto stop_sample_auto_started_playback = [this]() {
      if (record_arm_auto_started_playback_) togglePlaying();
      record_arm_auto_started_playback_ = false;
    };
    if (isRecording()) {
      // A SampleTrack take, Live View target or not - see the
      // "arm something new" section below for why this is still a single-
      // target flow, unlike per-track Live View note capture.
      // getRecordingTrackId() has to be captured before finishSampleCapture()
      // runs, not after - nothing guarantees it's still meaningful once
      // that's done.
      auto sample_track_id = getRecordingTrackId();
      finishSampleCapture();
      stop_sample_auto_started_playback();
      clearClipRecordingTake(sample_track_id); // pure bookkeeping - finishSampleCapture() already finalized the real clip
      return;
    }
    if (isThresholdArmed()) {
      auto sample_track_id = getRecordingTrackId();
      disarmThresholdRecording();
      stop_sample_auto_started_playback();
      clearClipRecordingTake(sample_track_id);
      return;
    }
    if (isNoteCaptureArmed()) {
      // Ordinary (non-Live-View) note capture only - Live View's own
      // per-track recording no longer touches this flag at all (see
      // armed_track_ids_' own doc comment). LaunchpadManager's own
      // refresh() reacts to this falling edge for its own bookkeeping
      // (stopping an auto-started transport).
      disarmNoteCapture();
      return;
    }

    // Nothing armed - arm whatever the currently selected track actually
    // needs. Live View focused means "the track/clip slot its own
    // cursor is on" (setClipGridCursor(), kept current by
    // UI::renderComponents() every frame) instead of the ordinary shared
    // track cursor.
    auto & song = getSong();
    int track_id;
    if (clip_grid_focused_) {
      if (clip_grid_track_id_ < 0 || clip_grid_clip_index_ < 0) return; // no valid slot to target
      track_id = clip_grid_track_id_;
    } else {
      track_id = song.getCurrentTrackId();
    }
    if (track_id < 0) return;
    auto * track = song.getMasterTrack().getChildByInternalId(track_id);
    if (!track) return;

    if (track->getType() == TrackType::SAMPLE) {
      // SampleTrack recording (Live View target or not) stays the
      // single-target flow it already is - see armed_track_ids_' own doc
      // comment for why this isn't unified with per-track note capture
      // yet. Populating the clip slot directly, with no arrangement
      // placement or transport start, mirrors the ordinary
      // transport-tied behavior it otherwise gets.
      if (clip_grid_focused_) armClipTrackRecording(track_id, clip_grid_clip_index_);
      armThresholdRecording(track_id);
      // No auto-started transport for a clip take - Player.cpp
      // already starts ALSA capture off isThresholdArmed() alone,
      // independent of isPlaying(), so there's nothing the transport needs
      // to be running for.
      if (!clip_grid_focused_ && !getPlaybackInfo().isPlaying()) startAutoRecordPlayback(record_arm_auto_started_playback_);
    } else if (clip_grid_focused_) {
      // Per-track Live View note capture: arming alone starts nothing
      // (toggleTrackArmed() is pure readiness, and already stops/finalizes
      // any in-flight take on the way to disarming - see its own doc
      // comment) - the actual take only begins later, quantized, the
      // moment a real pad press lands
      // (ClipPlayer::triggerClip()).
      toggleTrackArmed(track_id);
    } else {
      // The transport itself starts via LaunchpadManager's own refresh()
      // rising-edge detection (startAutoRecordSession()'s own
      // by-reference bookkeeping is LaunchpadManager-private) for the
      // ordinary (non-Live-View) case - nothing else to do here.
      armNoteCapture();
    }
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
  auto & key = active_buffer_name_;
  playback_infos_[key] = playback_info;
  recording_track_ids_[key] = recording_track_id;
  local_position_edit_seqs_[key] = local_position_edit_seq_;
  focused_clip_ids_[key] = focused_clip_id_;
  focused_clip_track_ids_[key] = focused_clip_track_id_;
}

void
Controller::loadActiveBufferState(const string & name) {
  auto & key = name;
  playback_info = playback_infos_[key];
  recording_track_id = recording_track_ids_[key];
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
  local_position_edit_seqs_.erase(name);
  focused_clip_ids_.erase(name);
  focused_clip_track_ids_.erase(name);
}

void
Controller::stopFocusedClipPreview() {
  if (focused_clip_track_id_ < 0) return;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_ALL_NOTES, getActiveBufferName(), focused_clip_track_id_));
}

bool
Controller::toggleDrumClipFocus(int track_id, int clip_index) {
  auto song = getCurrentSong();
  if (!song) return false;
  auto * track = song->getMasterTrack().getChildByInternalId(track_id);
  if (!track || (track->getType() != TrackType::PERCUSSION_CONTROL && track->getType() != TrackType::INSTRUMENT_CONTROL)) return false;
  if (clip_index < 0) return false;

  auto clips = song->getClips(track_id);
  if (clip_index < static_cast<int>(clips.size())) {
    auto existing = clips[static_cast<size_t>(clip_index)];
    if (getFocusedClipTrackId() == track_id && getFocusedClip() == existing.getId()) {
      // Pressing this same gesture again on the clip already open for
      // editing closes it instead of doing nothing - otherwise there'd be
      // no way back to the track's own background pattern via this same
      // gesture (resolveEditTarget()'s own no-focus fallback already
      // handles that once nothing's focused) or to open a different
      // clip's own slot instead.
      clearFocusedClip();
      if (drum_edit_requested_) drum_edit_requested_(track_id, false);
      return true;
    }
    setFocusedClip(track_id, existing.getId());
  } else {
    // Past the track's own current clip list - lazily creates a fresh,
    // empty, looping clip the same way ensureNoteRecordingClip() does for
    // a brand new take, ready to have steps entered directly rather than
    // needing a separate "new clip" gesture first - the same bar length
    // as everything else in the song, not clamped to a connected
    // Launchpad's own fixed 8-column grid: LaunchpadManager's own
    // step-grid paging (DeviceState::drum_edit_page, see its own comment)
    // is what lets a longer clip actually be seen/edited there, one
    // 8-step window at a time (or several windows at once, split across
    // multiple connected devices).
    auto next_ordinal = clips.size() + 1;
    auto scene = static_cast<int>(clips.size());
    auto clip = song->addClip(Clip(track_id));
    clip.setName(fmt::format("Clip {}", next_ordinal));
    clip.setLooping(true);
    clip.setLength(song->getSceneBarRows(scene)); // a bar of the scene's own time signature
    setFocusedClip(track_id, clip.getId());
  }
  if (drum_edit_requested_) drum_edit_requested_(track_id, true);
  return true;
}

void
Controller::addBuffer(std::shared_ptr<Song> song, const string & name, Version saved_version) {
  saveActiveBufferState();
  // The real-time player renders this song: it sees only finished edits.
  song->setContentPublished(true);
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
  // the live playback_info/recording_track_id scalars stay exactly as they are; only the *old* key's own map slot
  // (if any, from a previous session under a different name) needs
  // dropping so it doesn't linger as dead weight under a name nothing
  // will ever look up again.
  auto old_name = active_buffer_name_;
  dropBufferState(old_name);
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    auto song = songs_.at(old_name);
    songs_.erase(old_name);
    last_saved_versions_.erase(old_name);
    songs_[new_name] = std::move(song);
    last_saved_versions_[new_name] = saved_version;
    active_buffer_name_ = new_name;
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
  saveActiveBufferState();
  auto & song_id = name;
  bool created = false;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    if (songs_.find(song_id) == songs_.end()) {
      // Not open yet - create it fresh, same starter content "New" used
      // to set up back when it was its own command (see this method's own
      // doc comment on Controller.h).
      auto song = make_shared<Song>();
      song->addTrack(make_unique<InstrumentTrack>(0));
      song->setContentPublished(true);
      last_saved_versions_[song_id] = song->getVersion();
      songs_[song_id] = std::move(song);
      created = true;
    }
    active_buffer_name_ = name;
  }
  loadActiveBufferState(song_id);
  if (created) refreshBufferCommands();
  if (buffer_change_listener_) buffer_change_listener_();
}

void
Controller::cycleBuffer(bool forward) {
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
  // Always keep at least one buffer open.
  if (songs_.size() <= 1) return false;

  auto song_id = active_buffer_name_;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    songs_.erase(song_id);
    last_saved_versions_.erase(song_id);
  }

  dropBufferState(song_id);
  auto remaining = getBufferNames(); // already reflects the song_id erase above
  active_buffer_name_ = remaining.front(); // name-sorted first remaining buffer
  loadActiveBufferState(active_buffer_name_);
  refreshBufferCommands();
  // Drops the killed song's own live SongState, if it had one (Player::
  // handlePlaybackControlEvent()) - also stops it automatically if it
  // happened to be the playing buffer, simply by no longer existing to
  // render at all, rather than needing a separate STOP first.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::BUFFER_KILLED, song_id));
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
  if (!info.isPlaying()) stopAnyActiveRecordArm();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(info.isPlaying() ? PlaybackControlEvent::PLAY : PlaybackControlEvent::STOP, getActiveBufferName()));
  setPlaybackInfo(info);
  return info.isPlaying();
}

void
Controller::stopAnyActiveRecordArm() {
  if (isRecording()) {
    finishSampleCapture();
    record_arm_auto_started_playback_ = false;
    return;
  }
  if (isThresholdArmed()) {
    disarmThresholdRecording();
    record_arm_auto_started_playback_ = false;
    return;
  }
  if (isNoteCaptureArmed()) disarmNoteCapture();
}

void
Controller::moveEditPosition(int delta_rows) {
  auto info = getPlaybackInfo();
  // Row navigation only ever runs while stopped (see PatternEditor's own
  // "Row navigation while stopped" comment). Clamped to the rows the
  // arrangement can address (Song::clampArrangementRow()) - Player::
  // handlePlaybackControlEvent() applies the same clamp on the
  // audio-thread side to the MOVE_POSITION event this pushes below,
  // rather than trusting a value computed by the UI thread across the
  // thread boundary.
  info.setAbsolutePos(Song::clampArrangementRow(info.getAbsolutePosition() + delta_rows));
  info.setPositionEditSeq(++local_position_edit_seq_);
  setPlaybackInfo(info);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, getActiveBufferName(), delta_rows));
}

void
Controller::setEditPosition(int absolute_row) {
  auto info = getPlaybackInfo();
  // See moveEditPosition()'s own comment - the same clamp on both sides.
  info.setAbsolutePos(Song::clampArrangementRow(absolute_row));
  info.setPositionEditSeq(++local_position_edit_seq_);
  setPlaybackInfo(info);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_POSITION, getActiveBufferName(), absolute_row));
}

void
Controller::receivePlaybackSnapshot(const string & buffer_name, const PlaybackInfo & info) {
  mirrorSceneChange(buffer_name, info);
  if (buffer_name != active_buffer_name_) {
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
  auto merged = info;
  if (info.getPositionEditSeq() < local_position_edit_seq_) {
    // Stale: the audio thread took this snapshot before draining our most
    // recent moveEditPosition()/setEditPosition() control event. Keep
    // every other field from the real snapshot (voice counts, is_playing,
    // meters, ...) but preserve the local, already-correct edit-position
    // fields rather than regressing them - see this method's own doc
    // comment on Controller.h.
    merged.setAbsolutePos(playback_info.getAbsolutePosition());
    merged.setPositionEditSeq(playback_info.getPositionEditSeq());
  }
  if (info.getLiveSeq() < playback_info.getLiveSeq()) {
    // The same for Live View: ClipPlayer's prediction of a launch
    // or stop the audio thread hasn't applied yet stands.
    merged.setLiveTracks(playback_info.getLiveTracks());
    merged.setLiveSeq(playback_info.getLiveSeq());
  }
  setPlaybackInfo(merged);
  // Position fields aside (the only thing the merge above ever touches),
  // `info`'s own per-track data is exactly what actually arrived - the
  // live engine's own real, possibly-still-gliding Send Main/A/B/azimuth,
  // not wherever it's ultimately headed. Mirror it into the model so
  // nothing that reads a LeafTrack's own sends_/azimuth (Launchpad's own
  // LED refresh included - it still just reads the model, unchanged) ever
  // shows a value the engine hasn't actually reached yet.
  syncLiveGlideStateIntoModel(buffer_name, info);
}

// A launched scene's tempo and running time signature are applied by the
// audio thread (SongState::queueSceneChange()); the song's copies, which
// the UI shows and saves, follow once a snapshot reports the change - and
// only then, so a tempo edited since isn't overwritten by an older snapshot.
void Controller::mirrorSceneChange(const string & buffer_name, const PlaybackInfo & info) {
  auto & mirrored = mirrored_scene_seq_[buffer_name];
  if (info.getSceneSeq() <= mirrored) return;
  mirrored = info.getSceneSeq();
  auto song = getSongByName(buffer_name);
  if (!song) return;
  song->setRunningBars(info.getRunningBars());
  if (info.getTempo() > 0 && info.getTempo() != song->getTempo()) {
    Song::Edit edit(*song, "scene tempo", Song::Edit::Kind::STRUCTURE, Song::Edit::Origin::SYNC);
    song->setTempo(static_cast<short>(info.getTempo()));
    if (buffer_name == active_buffer_name_) getUIEventQueue().push(make_unique<LogEvent>("Tempo " + to_string(song->getTempo())));
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

void
Controller::syncLiveGlideStateIntoModel(const string & buffer_name, const PlaybackInfo & info) {
  auto song = getSongByName(buffer_name);
  if (!song) return;
  for (auto track_id : song->getPlayableTrackIds()) {
    auto & track_info = info.getTrackInfo(track_id);
    auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
    if (!leaf_track) continue;
    if (track_info.hasLiveSends()) {
      auto sends = leaf_track->getSends();
      // Only actually write (and only open a Song::Edit - the "unsaved
      // changes" signal) when something really changed - every snapshot
      // arrives whether or not any track is currently gliding, and
      // re-asserting an already-correct value on every single one would
      // mark the song dirty continuously while nothing is actually
      // happening.
      bool changed = sends.main != track_info.getLiveSendMain() || sends.a != track_info.getLiveSendA() || sends.b != track_info.getLiveSendB();
      if (changed) {
        Song::Edit edit(*song, "glide sends", Song::Edit::Kind::STRUCTURE, Song::Edit::Origin::SYNC);
        leaf_track->setSendMain(track_info.getLiveSendMain());
        leaf_track->setSendA(track_info.getLiveSendA());
        leaf_track->setSendB(track_info.getLiveSendB());
      }
    }
    if (track_info.hasLiveAzimuth() && leaf_track->getAzimuth() != track_info.getLiveAzimuth()) {
      Song::Edit edit(*song, "glide azimuth", Song::Edit::Kind::STRUCTURE, Song::Edit::Origin::SYNC);
      leaf_track->setAzimuth(track_info.getLiveAzimuth());
    }
  }
}

bool
Controller::toggleTrackMuted(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return false;
  Song::Edit edit(*song, "toggle mute");
  leaf_track->setMuted(!leaf_track->isMuted());
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MUTED, getActiveBufferName(), track_id, leaf_track->isMuted() ? 1 : 0));
  return leaf_track->isMuted();
}

bool
Controller::toggleTrackSolo(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return false;
  Song::Edit edit(*song, "toggle solo");
  leaf_track->setSolo(!leaf_track->isSolo());
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SOLO, getActiveBufferName(), track_id, leaf_track->isSolo() ? 1 : 0));
  return leaf_track->isSolo();
}

void
Controller::cycleTrackMonitor(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  using Monitor = LeafTrack::Monitor;
  auto m = leaf_track->getMonitor();
  m = m == Monitor::AUTO ? Monitor::IN : m == Monitor::IN ? Monitor::OFF : Monitor::AUTO;
  Song::Edit edit(*song, "cycle monitor");
  leaf_track->setMonitor(m);
  getUIEventQueue().push(make_unique<LogEvent>(m == Monitor::IN ? "Monitor: In" : m == Monitor::OFF ? "Monitor: Off" : "Monitor: Auto"));
}

std::string
Controller::saveDeviceSettings() {
  if (device_settings_path_.empty()) return "";
  if (::saveDeviceSettings(device_settings_path_, device_settings_)) return "";
  return "couldn't save the selection to " + device_settings_path_;
}

Controller::DeviceChange
Controller::setCaptureDevice(const std::string & name) {
  if (isRecording() || isThresholdArmed() || !monitored_track_ids_.empty()) {
    return {false, "Can't change the capture device while recording or monitoring"};
  }
  auto value = isDefaultDevice(name) ? std::string() : name;
  device_settings_.capture = value;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_CAPTURE_DEVICE, value));
  return {true, saveDeviceSettings()};
}

Controller::DeviceChange
Controller::setPlaybackDevice(const std::string & name) {
  auto value = isDefaultDevice(name) ? std::string() : name;
  device_settings_.playback = value;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_PLAYBACK_DEVICE, value));
  return {true, saveDeviceSettings()};
}

Controller::DeviceChange
Controller::setMidiInput(const std::string & spec) {
  device_settings_.midi_input = spec;
  return {true, saveDeviceSettings()};
}

bool
Controller::isMonitoring(int track_id) const {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return true;
  switch (leaf_track->getMonitor()) {
  case LeafTrack::Monitor::IN: return true;
  case LeafTrack::Monitor::OFF: return false;
  case LeafTrack::Monitor::AUTO: break;
  }
  bool capture_armed_here = isNoteCaptureArmed() && track_id == song->getCurrentTrackId();
  bool threshold_armed_here = isThresholdArmed() && track_id == getRecordingTrackId();
  if (isTrackArmed(track_id) || capture_armed_here || threshold_armed_here) return true;
  bool anything_armed = hasAnyTrackArmed() || isNoteCaptureArmed() || isThresholdArmed();
  return !anything_armed && leaf_track->getType() != TrackType::SAMPLE;
}

void
Controller::verifyPublishedContent() {
  static const bool enabled = std::getenv("SYNTH_VERIFY_CONTENT") != nullptr;
  if (!enabled) return;
  std::vector<std::pair<std::string, std::shared_ptr<Song> > > open_songs;
  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    for (auto & [ name, song ] : songs_) open_songs.emplace_back(name, song);
  }
  for (auto & [ name, song ] : open_songs) {
    if (song->publishedContentIsCurrent()) {
      stale_content_warned_.erase(name);
    } else if (stale_content_warned_.insert(name).second) {
      getUIEventQueue().push(make_unique<LogEvent>("Playback content is stale in " + name + ": a write skipped Song::Edit"));
    }
  }
}

void
Controller::syncMonitoring() {
  verifyPublishedContent();
  auto buffer = getActiveBufferName();
  auto & queue = getPlaybackEventQueue();
  if (buffer != monitored_buffer_) {
    for (auto track_id : monitored_track_ids_) {
      queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MONITORING, monitored_buffer_, track_id, 0));
    }
    monitored_track_ids_.clear();
    monitored_buffer_ = buffer;
  }

  auto & song = getSong();
  std::unordered_set<int> now;
  for (auto track_id : song.getPlayableTrackIds()) {
    auto track = song.getMasterTrack().getChildByInternalId(track_id);
    if (track && track->getType() == TrackType::SAMPLE && isMonitoring(track_id)) now.insert(track_id);
  }
  for (auto track_id : monitored_track_ids_) {
    if (!now.count(track_id)) queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MONITORING, buffer, track_id, 0));
  }
  for (auto track_id : now) {
    if (!monitored_track_ids_.count(track_id)) queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MONITORING, buffer, track_id, 1));
  }
  if (!now.empty() && !monitor_feedback_warned_) {
    monitor_feedback_warned_ = true;
    getUIEventQueue().push(make_unique<LogEvent>("Monitoring audio input - mind feedback through open speakers"));
  }
  monitored_track_ids_ = std::move(now);
}

bool
Controller::toggleTrackCollapsed(int track_id) {
  auto song = getCurrentSong();
  auto track = song->getMasterTrack().getChildByInternalId(track_id);
  if (!track) return false;
  Song::Edit edit(*song, "toggle collapsed");
  track->setCollapsed(!track->isCollapsed());
  return track->isCollapsed();
}

void
Controller::setTrackSendA(int track_id, float value) {
  // Any track, the master included (its Send A/B are the bus's returns).
  auto song = getCurrentSong();
  auto track = song->getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;
  float linear = dbToLinear(value);
  Song::Edit edit(*song, "set send A");
  track->setSendA(linear);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_A, getActiveBufferName(), track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendB(int track_id, float value) {
  // Any track, the master included (its Send A/B are the bus's returns).
  auto song = getCurrentSong();
  auto track = song->getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;
  float linear = dbToLinear(value);
  Song::Edit edit(*song, "set send B");
  track->setSendB(linear);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_B, getActiveBufferName(), track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendMain(int track_id, float value) {
  // Any track, the master included (its Send Main is the dry mix's level).
  auto song = getCurrentSong();
  auto track = song->getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;
  float linear = dbToLinear(value);
  Song::Edit edit(*song, "set send main");
  track->setSendMain(linear);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_MAIN, getActiveBufferName(), track_id, static_cast<int>(linear * 1000.0f + 0.5f)));
}

// Deliberately no model write here at all - see this method's own header
// comment. duration_seconds encoded in milliseconds (fits comfortably in
// an int; every existing glide is well under a second), matching
// SET_TRACK_SEND_A/B/MAIN's own fixed-point-int convention for the target.
void
Controller::glideTrackSendA(int track_id, float target_db, float duration_seconds) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  // target_db passed straight through, in tenths of a dB (same fixed-
  // point convention SET_TRACK_AZIMUTH's own degrees use) - not converted
  // to linear here the way the instant setters above are. The engine's
  // own ramp interpolates in dB (LeafTrackState.h's own comment on why -
  // the Launchpad row layout this is driven from is itself linear in dB,
  // so that's the space a glide needs to move at a constant rate through),
  // converting to linear only once it's actually applied.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::GLIDE_TRACK_SEND_A, getActiveBufferName(), track_id,
    static_cast<int>(target_db * 10.0f + (target_db >= 0.0f ? 0.5f : -0.5f)), static_cast<int>(std::max(0.0f, duration_seconds) * 1000.0f + 0.5f)));
}

void
Controller::glideTrackSendB(int track_id, float target_db, float duration_seconds) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::GLIDE_TRACK_SEND_B, getActiveBufferName(), track_id,
    static_cast<int>(target_db * 10.0f + (target_db >= 0.0f ? 0.5f : -0.5f)), static_cast<int>(std::max(0.0f, duration_seconds) * 1000.0f + 0.5f)));
}

void
Controller::glideTrackSendMain(int track_id, float target_db, float duration_seconds) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::GLIDE_TRACK_SEND_MAIN, getActiveBufferName(), track_id,
    static_cast<int>(target_db * 10.0f + (target_db >= 0.0f ? 0.5f : -0.5f)), static_cast<int>(std::max(0.0f, duration_seconds) * 1000.0f + 0.5f)));
}

void
Controller::glideTrackAzimuth(int track_id, float target_degrees, float duration_seconds) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  // Tenths-of-a-degree, same fixed-point convention setTrackAzimuth() uses.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::GLIDE_TRACK_AZIMUTH, getActiveBufferName(), track_id,
    static_cast<int>(target_degrees * 10.0f + (target_degrees >= 0.0f ? 0.5f : -0.5f)), static_cast<int>(std::max(0.0f, duration_seconds) * 1000.0f + 0.5f)));
}

void
Controller::setBusEffectKind(int slot, BusEffectKind kind) {
  auto song = getCurrentSong();
  Song::Edit edit(*song, "set bus effect");
  song->setBusSlotKind(slot, kind);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_BUS_EFFECT, getActiveBufferName(), slot, static_cast<int>(kind)));
}

void
Controller::setTrackAzimuth(int track_id, float value) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  Song::Edit edit(*song, "set azimuth");
  leaf_track->setAzimuth(value);
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
  Song::Edit edit(*song, "add note column");
  leaf_track->setMinNoteColumns(leaf_track->getMinNoteColumns() + 1);
}

void
Controller::removeNoteColumn(int track_id) {
  auto song = getCurrentSong();
  auto leaf_track = asLeafTrack(song->getMasterTrack().getChildByInternalId(track_id));
  if (!leaf_track) return;
  Song::Edit edit(*song, "remove note column");
  leaf_track->setMinNoteColumns(leaf_track->getMinNoteColumns() - 1);
}

void
Controller::ensureRowCleared(std::set<std::pair<int, int>> & cleared_rows, int row, int track_id) {
  auto song = getCurrentSong();
  // Clears whatever's actually active at (track_id, row) - a placed
  // clip instance's own leaf Pattern, live-linked to every other
  // placement of it, or the track's own background Pattern otherwise
  // (ArrangementOps.h's own resolveEditTarget()) - the same resolution
  // every write into a live take already goes through
  // (applyNotePressure()/writeReleaseOff(), LaunchpadManager's own
  // note-on write), so the row actually cleared is always the one a
  // fresh take is about to write into, clip-based recording included.
  //
  // A shorter-than-its-own-container Pattern repeats - see Pattern.h's own
  // getEffectiveRow() comment - so the row actually cleared is this
  // track's own effective one, not necessarily the raw playhead row.
  // cleared_rows itself is keyed by that same effective row too, not the
  // raw one: two different raw rows that repeat the same underlying
  // content (e.g. rows 4 and 20 of a 16-row pattern, or two laps of a
  // looping clip) must dedup together - otherwise sweeping past the
  // second one would clear (and lose) a note the first one just wrote,
  // one loop iteration into the same live take.
  auto target = resolveEditTarget(*song, track_id, row, getFocusedClip());
  if (!cleared_rows.insert({target.effective_row, track_id}).second) return; // already cleared this session
  Song::Edit edit(*song, "clear row for take");
  target.pattern->setNotes(target.effective_row, {});
}

void
Controller::sweepAutoRecordRows(std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, int new_row, const std::vector<int> & track_ids) {
  if (new_row < last_cleared_row) last_cleared_row = new_row - 1;
  if (new_row <= last_cleared_row) return; // nothing new to sweep

  for (int row = last_cleared_row + 1; row <= new_row; row++) {
    for (auto track_id : track_ids) {
      ensureRowCleared(cleared_rows, row, track_id);
    }
  }
  last_cleared_row = new_row;
}

void
Controller::ensureNoteRecordingClip(std::unordered_map<int, std::string> & clip_ids, int track_id, int row) {
  if (!getFocusedClip().empty()) return; // already resolves into the focused clip directly - nothing to place
  auto song = getCurrentSong();
  // previousBarRow(), not the live row itself - a brand new clip's own
  // placement is bar-quantized the same as every other real placement in
  // this song, but rounded back rather than forward (that function's own
  // comment on why): a live take's first note has to land inside whatever
  // clip gets created for it, and the clip can't start later than that
  // note's own row.
  row = previousBarRow(song->getArrangementBars(), row);
  auto rows_per_bar = song->getArrangementBars().barRows();
  auto active = resolveInstanceAt(*song, track_id, row);
  if (active.clip_index >= 0) return; // a real clip is already active here - write into it, same as ordinary editing

  Song::Edit edit(*song, "start take clip");
  Clip clip(track_id);
  clip.setName(fmt::format("Take {}", song->getClips(track_id).size() + 1));
  // Non-looping by default - a live take is one specific performance, not
  // a pattern meant to repeat automatically the moment it ends; looping it
  // is the performer's own later call to make (Live View), not this
  // clip's own starting assumption.
  clip.setLooping(false);
  // A full bar's worth of length right away, not left at 0 (Clip.h's
  // "not given one yet") - the clip's own placement was just rounded back
  // to this bar's start, so the live row the very first note is about to
  // land on can be anywhere up to a whole bar past that. Left at the
  // default, a non-looping clip with no explicit length falls back to
  // treating itself as 1 row long (resolveInstanceAt()'s own one-shot-
  // expiry fallback) - already "expired" by the time that first note's own
  // write re-resolves against it, silently missing the very clip
  // ensureNoteRecordingClip() just created. extendRecordingClipsIfNeeded()
  // takes over growing it further from here as the take continues.
  clip.setLength(rows_per_bar);
  auto added = song->addClip(std::move(clip));
  auto clip_index = static_cast<int>(song->getClips(track_id).size()) - 1;
  placeClipInstance(*song, track_id, row, clip_index);
  clip_ids[track_id] = added.getId();
}

void
Controller::extendRecordingClipsIfNeeded(std::unordered_map<int, std::string> & clip_ids, const std::vector<int> & held_track_ids) {
  if (clip_ids.empty()) return;
  auto & info = getPlaybackInfo();
  if (!info.isPlaying()) return;

  auto song = getCurrentSong();
  if (!song) return;
  auto arrangement = song->getArrangement();
  auto rows_per_bar = song->getArrangementBars().barRows();

  for (auto & [ track_id, clip_id ] : clip_ids) {
    // Only while this track actually has a note held right now - see this
    // method's own header comment for why.
    if (std::find(held_track_ids.begin(), held_track_ids.end(), track_id) == held_track_ids.end()) continue;

    // Found by its own stable id, directly in the track's own instance
    // map - not resolveInstanceAt()'s own "what's active right now"
    // query. A stray stop or a different clip may have already landed
    // ahead of this one (leftover authoring, or just something this same
    // take is about to grow across) - resolveInstanceAt() would report
    // *that* at the live row instead, even though this clip's own
    // placement is still sitting exactly where it was put; this needs to
    // find its own placement regardless of whether something else is
    // currently superseding it, or growth (and the overwrite below) could
    // never get past the very first obstacle in its way.
    auto instances = arrangement.getInstancesForTrack(track_id);
    int start_row = -1;
    for (auto & [ row, id ] : instances) {
      if (id == clip_id) { start_row = row; break; }
    }
    if (start_row < 0) continue; // no longer placed at all - defensive, shouldn't happen mid-session

    auto clips = song->getClips(track_id);
    int clip_index = -1;
    for (size_t i = 0; i < clips.size(); i++) {
      if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
    }
    if (clip_index < 0) continue; // the clip itself is gone - defensive
    auto clip = clips[static_cast<size_t>(clip_index)];

    // Grows a full bar at a time until at least one bar of headroom
    // remains ahead of the current row - a single bounded loop (never
    // more than a couple of iterations at any sane rows_per_bar) rather
    // than relying on this method being called again soon enough to
    // finish the job: this is called once per rendered audio block, many
    // times a row, so in practice one bar of growth per call would
    // already keep up - but "keeps up in practice" isn't the same as
    // "always leaves the window in a valid, sufficiently-ahead state the
    // instant this call returns", which one-shot expiry (resolveInstanceAt())
    // actually depends on.
    Song::Edit edit(*song, "extend take clip");
    bool grew = false;
    auto window_last_row = start_row + std::max(1, clip.getLength()) - 1;
    while (window_last_row - info.getAbsolutePosition() < rows_per_bar) {
      clip.setLength(std::max(1, clip.getLength()) + rows_per_bar);
      window_last_row = start_row + clip.getLength() - 1;
      grew = true;
    }
    if (grew) {
      // A live take overwrites whatever else is in its own path as it
      // keeps growing, the same "replaces, not merges with, whatever's
      // already there" rule ensureRowCleared() already applies to the
      // background Pattern - extended here to the arrangement layer
      // itself, so a stray stop (or a different clip) this take grows
      // across doesn't keep silencing/interrupting it. placeClipInstance()
      // already sweeps every other instance event within a clip's own
      // reach on every call, keyed off whatever its length currently is -
      // re-running it here, now that length just grew, is all this needs.
      placeClipInstance(*song, track_id, start_row, clip_index);
    } else {
      edit.discard();
    }
  }
}

int
Controller::ensureClipRecordingClip(int track_id, int absolute_step, int bar_start_step) {
  auto it = clip_recording_takes_.find(track_id);
  if (it == clip_recording_takes_.end() || it->second.clip_index < 0) return -1;
  auto & take = it->second;
  auto song = getCurrentSong();
  if (!song) return -1;
  auto clips = song->getClips(track_id);

  if (!take.clip_ready) {
    // An already-primed origin means this is an overdub take (see
    // primeClipRecordingOrigin()'s own comment) - the target clip is
    // already actively playing, so its own content/length/looping state
    // stay exactly as they are; only a fresh take (an empty slot, or a
    // genuinely idle occupied one) resets/creates the clip.
    take.is_overdub = take.origin_step >= 0;
    if (!take.is_overdub) {
      // Whichever clip already sits at exactly the pressed index - real
      // content, a filler Song::ensureClipAt() already placed there
      // earlier, or one it pads in fresh right now - never retargeted to
      // wherever the next unused slot happens to be: holes are allowed,
      // so the exact scene index pressed is always the recording target.
      // "Same id, so anything else already referencing it keeps pointing
      // at it, but its own prior content is gone the moment a fresh take
      // actually starts writing, not merged with it" still applies to a
      // clip that already had real content; a still-id-less filler gets a
      // real id/name here instead, the same "assign one if it doesn't
      // already have one" convention addClip() itself uses.
      auto existing = song->ensureClipAt(track_id, take.clip_index);
      existing.getLeafPattern().assign(Pattern());
      existing.setLength(0);
      existing.setLooping(false);
      if (existing.getId().empty()) {
        existing.setId(song->generateUniqueClipId());
        existing.setName(fmt::format("Take {}", take.clip_index + 1));
      }
    }
    take.clip_ready = true;
    // primeClipRecordingOrigin() may already have fixed this take's own
    // row 0 (an overdub - see its own comment); otherwise, row 0 is this
    // bar's own start, not the exact step this first note happened to
    // land on - a performer may deliberately start playing on the bar's
    // second beat rather than its first, and the clip's own loop point
    // still has to be the bar boundary either way.
    if (take.origin_step < 0) {
      take.origin_step = bar_start_step >= 0 ? bar_start_step : song->barStartAtOrBefore(absolute_step);
    }
  }
  extendClipRecordingClipIfNeeded(track_id, absolute_step); // no-op for an overdub take - see its own comment
  auto row = absolute_step - take.origin_step;
  if (take.is_overdub && take.clip_index < static_cast<int>(clips.size())) {
    // Wrapped, not left to grow - see this method's own doc comment on
    // why an overdub's own row cycles through the clip's already-fixed
    // length instead.
    auto length = std::max(1, clips[static_cast<size_t>(take.clip_index)].getLength());
    row = ((row % length) + length) % length;
  }
  return row;
}

void
Controller::extendClipRecordingClipIfNeeded(int track_id, int absolute_step) {
  auto it = clip_recording_takes_.find(track_id);
  if (it == clip_recording_takes_.end() || !it->second.clip_ready || it->second.is_overdub) return;
  auto & take = it->second;
  auto song = getCurrentSong();
  if (!song) return;
  auto clips = song->getClips(track_id);
  if (take.clip_index < 0 || take.clip_index >= static_cast<int>(clips.size())) return;
  auto clip = clips[static_cast<size_t>(take.clip_index)];

  auto row = absolute_step - take.origin_step;
  auto rows_per_bar = song->barRowsAt(absolute_step);
  auto window_last_row = std::max(1, clip.getLength()) - 1;
  while (window_last_row - row < rows_per_bar) {
    clip.setLength(std::max(1, clip.getLength()) + rows_per_bar);
    window_last_row = clip.getLength() - 1;
  }
}

void
Controller::trimClipRecordingClip(int track_id) {
  auto it = clip_recording_takes_.find(track_id);
  if (it == clip_recording_takes_.end()) return;
  auto take = it->second; // copied out - the map entry itself is gone below
  clip_recording_takes_.erase(it);
  if (!take.clip_ready) return;
  // An overdub take never touched the clip's own length/looping state at
  // all - it was already correct, and the clip was already playing,
  // uninterrupted, throughout - so there's nothing to trim, and nothing
  // new for LaunchpadManager to join into Live View's live performance
  // (it was already live the whole time).
  if (take.is_overdub) return;
  auto song = getCurrentSong();
  if (!song) return;
  auto clips = song->getClips(track_id);
  if (take.clip_index < 0 || take.clip_index >= static_cast<int>(clips.size())) return;
  auto clip = clips[static_cast<size_t>(take.clip_index)];

  Song::Edit edit(*song, "finish take clip");
  int last_row = -1;
  for (auto & [ row, notes ] : clip.getLeafPattern().getNotesByRow()) last_row = std::max(last_row, static_cast<int>(row));
  auto rows_per_bar = song->barRowsAt(song->barStartAtOrBefore(take.origin_step));
  // Nothing actually landed (the take was armed and disarmed with no note
  // ever written) - one bar, matching every other freshly-created clip's
  // own minimum length rather than a zero-length one.
  clip.setLength(last_row < 0 ? rows_per_bar : (last_row / rows_per_bar + 1) * rows_per_bar);

  // A fresh take is meant to be played straight back, live, the instant it
  // finishes - looping (rather than ensureClipRecordingClip()'s own
  // recording-time setLooping(false), meaningless once a take is actually
  // over) and pushed here for LaunchpadManager to pick up and join into
  // Live View's live performance immediately (see
  // takeCompletedClipRecording()'s own comment).
  clip.setLooping(true);
  completed_clip_recordings_.push_back({ track_id, take.clip_index });
}

void
Controller::extendRecordingSampleClipIfNeeded() {
  if (!hasRecordingClip() || recording_start_row_ < 0) return;
  auto & info = getPlaybackInfo();
  if (!info.isPlaying()) return;

  auto song = getCurrentSong();
  if (!song) return;
  auto track_id = getRecordingTrackId();
  auto clips = song->getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == recording_clip_id_) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return; // defensive - shouldn't happen mid-session
  auto clip = clips[static_cast<size_t>(clip_index)];

  // Same growth-loop shape as extendRecordingClipsIfNeeded() above - grows
  // a full bar at a time until at least one bar of headroom remains ahead
  // of the current row.
  auto rows_per_bar = song->getArrangementBars().barRows();
  Song::Edit edit(*song, "extend sample take clip");
  bool grew = false;
  auto window_last_row = recording_start_row_ + std::max(1, clip.getLength()) - 1;
  while (window_last_row - info.getAbsolutePosition() < rows_per_bar) {
    clip.setLength(std::max(1, clip.getLength()) + rows_per_bar);
    window_last_row = recording_start_row_ + clip.getLength() - 1;
    grew = true;
  }
  if (grew) {
    // Sweeps whatever's stale in this take's own newly-extended path (an
    // old stop marker, a superseded instance) the same way
    // extendRecordingClipsIfNeeded() already does - placeClipInstance()
    // clears every instance event within a clip's own reach on every
    // call, keyed off whatever its length currently is.
    placeClipInstance(*song, track_id, recording_start_row_, clip_index);
  } else {
    edit.discard();
  }
}

void
Controller::setSwing(int percent) {
  auto & song = getSong();
  Song::Edit edit(song, "set swing");
  song.setSwing(percent);
  getUIEventQueue().push(make_unique<LogEvent>("Swing " + to_string(song.getSwing()) + "%"));
}

void
Controller::setTempo(int bpm) {
  auto & song = getSong();
  Song::Edit edit(song, "set tempo");
  song.setTempo(static_cast<short>(std::clamp(bpm, 20, 300)));
  getUIEventQueue().push(make_unique<LogEvent>("Tempo " + to_string(song.getTempo())));
}

void
Controller::deleteClipSlot(int track_id, int clip_index) {
  auto & song = getSong();
  auto clips = song.getClips(track_id);
  // Clears any live preview/edit focus on the clip being deleted, so
  // getFocusedClip() isn't left pointing at an id nothing resolves to.
  if (clip_index >= 0 && clip_index < static_cast<int>(clips.size()) &&
      getFocusedClipTrackId() == track_id && getFocusedClip() == clips[static_cast<size_t>(clip_index)].getId()) {
    clearFocusedClip();
  }
  string name;
  if (deleteClipOrStopButton(song, track_id, clip_index, &name) == SlotDelete::CLIP) {
    getUIEventQueue().push(make_unique<LogEvent>("Deleted clip: " + (name.empty() ? string("(unnamed)") : name)));
  }
}

void
Controller::setMetronomeOn(bool on) {
  metronome_on_ = on;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_METRONOME, "", on ? 1 : 0));
}

void
Controller::startAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, int & last_cleared_row, std::unordered_map<int, std::string> & clip_ids) {
  togglePlaying();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_RECORDING_MUTE, getActiveBufferName(), 1));
  auto_started_playback = true;
  cleared_rows.clear();
  last_cleared_row = -1;
  clip_ids.clear();
}

void
Controller::startAutoRecordPlayback(bool & auto_started_playback) {
  togglePlaying();
  auto_started_playback = true;
}

void
Controller::stopAutoRecordSession(bool & auto_started_playback, std::set<std::pair<int, int>> & cleared_rows, const PlaybackInfo & info, std::unordered_map<int, std::string> & clip_ids) {
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
  clip_ids.clear();
}

void
Controller::writeReleaseOff(std::set<std::pair<int, int>> & cleared_rows, bool auto_started_playback, int row, int track_id, int note_column, int delay) {
  endNotePressure(track_id, note_column);
  if (auto_started_playback) ensureRowCleared(cleared_rows, row, track_id);
  auto song = getCurrentSong();
  auto target = resolveEditTarget(*song, track_id, row, getFocusedClip());
  Song::Edit edit(*song, "release note");
  target.pattern->setNote(target.effective_row, note_column, Note(0, 0, delay));
}

void
Controller::clearNoteCell(int row, int track_id, int note_column) {
  auto song = getCurrentSong();
  auto target = resolveEditTarget(*song, track_id, row, getFocusedClip());
  Song::Edit edit(*song, "clear note");
  target.pattern->setNote(target.effective_row, note_column, Note());
}

void
Controller::applyNotePressure(int row, int track_id, int note_column, short velocity, int delay) {
  auto song = getCurrentSong();
  auto target = resolveEditTarget(*song, track_id, row, getFocusedClip());
  auto note = target.pattern->getNote(target.effective_row, note_column);
  if (note.isDefined() && !note.isAftertouch()) return; // never overwrite a real note
  note.setDelay(delay);
  note.setVelocity(velocity);
  target.pattern->setNote(target.effective_row, note_column, note);
}

short
Controller::notePressure(int row, int track_id, int note_column, short velocity, int delay, const PressureWriter & write_row, const PressureRowSource & current_row) {
  constexpr int row_units = 256;
  constexpr int max_filled_rows = 256;
  auto clamp_pressure = [](int v) { return static_cast<short>(std::clamp(v, 1, 127)); };
  auto & st = pressure_states_[{ track_id, note_column }];
  delay = std::clamp(delay, 0, row_units - 1);

  if (st.row < 0 || row < st.row || row - st.row > max_filled_rows) {
    // First reading of this note (or the transport jumped): nothing earlier
    // to average with.
    st = PressureState { row, delay, delay, velocity, 0, {}, {} };
  } else {
    if (write_row) {
      for (int skipped = st.row + 1; skipped < row; skipped++) write_row(skipped, clamp_pressure(st.last_value));
    }
    if (row > st.row) {
      st.row = row;
      st.start = 0;
      st.last_time = 0;
      st.integral = 0;
    }
    delay = std::max(delay, st.last_time);
    st.integral += st.last_value * (delay - st.last_time);
    st.last_time = delay;
    st.last_value = velocity;
  }
  st.write_row = write_row;
  st.current_row = current_row;

  int span = row_units - st.start;
  auto pressure = clamp_pressure((st.integral + velocity * (row_units - delay) + span / 2) / span);
  if (write_row) write_row(row, pressure);
  return pressure;
}

void
Controller::tickNotePressure() {
  constexpr int max_filled_rows = 256;
  if (pressure_states_.empty()) return;
  auto song = getCurrentSong();
  if (!song) return;
  Song::Edit edit(*song, "note pressure");
  bool wrote = false;
  for (auto & entry : pressure_states_) {
    auto & st = entry.second;
    if (st.row < 0 || !st.write_row || !st.current_row) continue;
    int row = st.current_row();
    if (row <= st.row || row - st.row > max_filled_rows) continue;
    auto held = static_cast<short>(std::clamp(st.last_value, 1, 127));
    for (int entered = st.row + 1; entered <= row; entered++) st.write_row(entered, held);
    st.row = row;
    st.start = 0;
    st.last_time = 0;
    st.integral = 0;
    wrote = true;
  }
  if (!wrote) edit.discard();
}

void
Controller::beginSampleCapture(int track_id, int latency_frames) {
  auto song = getCurrentSong();
  if (!song || !current_sample || current_sample->numberOfFrames() == 0) return;

  auto clips = song->getClips(track_id);
  // Live View recording into an already-populated slot overdubs it -
  // same id/name, so anything already referencing it (an arrangement
  // instance elsewhere) keeps pointing at it, and every existing layer
  // stays completely untouched (Clip::addSampleLayer()'s own comment) -
  // real audio genuinely sums, the same "always merges rather than
  // replaces" decision note-based clip recording already settled, not
  // the destructive "generalize today's clear-and-restart behavior"
  // alternative. Lands at exactly the requested index - holes are allowed
  // (Song::ensureClipAt()), so a target past the list's own current end
  // backfills the gap with empty fillers instead of collapsing to
  // wherever a fresh append happens to land, the same fix
  // ensureClipRecordingClip() already has for note-based takes. An
  // ordinary (non-Live-View) take has no target index at all - it
  // always appends a brand new clip, same as before.
  Song::Edit edit(*song, "begin sample capture");
  auto take_it = clip_recording_takes_.find(track_id);
  bool is_clip_recording_take = take_it != clip_recording_takes_.end();
  bool targets_existing_slot = is_clip_recording_take && take_it->second.clip_index >= 0;
  ClipView clip = targets_existing_slot ? song->ensureClipAt(track_id, take_it->second.clip_index) : song->addClip(Clip(track_id));
  // A target index that already had real identity (real prior content, or
  // a pre-authored-but-still-empty placeholder - Song::ensureClipAt()'s
  // own doc comment) keeps its own id/name untouched below; a genuinely
  // id-less one (a fresh filler ensureClipAt() just backfilled/reused)
  // gets one assigned there instead, same as this take's own fresh append
  // always does.
  bool reuse_existing = targets_existing_slot && !clip.getId().empty();
  // An overdub - a new layer added alongside whatever's already there -
  // only when the target slot already had real audio in it (not merely
  // an existing, but still genuinely empty, id-less filler - reuse_existing
  // alone doesn't distinguish the two). Checked before anything below
  // touches the clip, since getSampleContent()/addSampleLayer() would
  // otherwise change what hasSample() itself reports.
  bool is_overdub = reuse_existing && clip.hasSample();

  // Non-looping by default - same reasoning as ensureNoteRecordingClip()'s
  // own identical call: a live take is one specific performance, not a
  // pattern meant to repeat automatically the moment it ends; looping it
  // is the performer's own later call to make (Live View), not this
  // clip's own starting assumption. Left alone for an overdub - the clip
  // was already looping/playing for this to even be an overdub in the
  // first place.
  if (!is_overdub) clip.setLooping(false);
  auto content = is_overdub ? clip.addSampleLayer() : clip.firstSampleLayer();
  // The *same* shared_ptr startRecording() already handed out, not a
  // copy - every later addToSample() call (mutating *current_sample in
  // place via AudioBuffer::append()) is visible through this clip's own
  // SampleContent automatically, ordinary shared_ptr aliasing giving
  // "live" sharing for free.
  content.setBuffer(current_sample);
  content.setOriginalTempo(song->getTempo());
  content.setNativeSampleRate(channel_config.getAudioOutSampleRate());
  // A reused (non-overdub) clip's own prior trim must never leak into
  // this new take - getSampleContent() on an existing clip returns its
  // existing layer-0 content, not a fresh one, unlike the brand-new-clip
  // and fresh-layer cases, where both already default to 0 anyway.
  content.setInPoint(latency_frames > 0 ? static_cast<float>(latency_frames) / static_cast<float>(channel_config.getAudioOutSampleRate()) : 0.0f);
  content.setOutPoint(0.0f);
  if (!reuse_existing) {
    // ensureClipAt() deliberately never assigns one to a filler - only
    // something that actually gives it real content does, here.
    if (clip.getId().empty()) clip.setId(song->generateUniqueClipId());
    clip.setName(fmt::format("Take {}", targets_existing_slot ? take_it->second.clip_index + 1 : static_cast<int>(clips.size()) + 1));
  }
  // A full bar's worth of length right away, not left at 0 (Clip.h's "not
  // given one yet") - the real, final duration isn't known until
  // finishSampleCapture(), but a non-looping clip with no explicit length
  // falls back to treating itself as 1 row long (resolveInstanceAt()'s
  // own one-shot-expiry check), which the transport's own per-row
  // scheduling re-evaluates continuously - left at 0, this take would
  // look "already expired" to it after its very first row, live, long
  // before finishSampleCapture() ever runs. extendRecordingSampleClipIfNeeded()
  // keeps growing this as the take continues, same as
  // ensureNoteRecordingClip()'s own identical starting point and
  // extendRecordingClipsIfNeeded()'s own growth for a note-recording take.
  // Skipped for an overdub - the clip's own length is already
  // established (that's what's actually looping/playing right now for
  // this to be an overdub at all), so restarting it back down to one bar
  // would shrink an already-longer clip out from under its own earlier
  // layers; finishSampleCapture() only ever grows it from here, never
  // shrinks it.
  if (!is_overdub) clip.setLength(song->getRunningTimeSignature().rowsPerBar());

  recording_clip_id_ = clip.getId();
  recording_latency_frames_ = latency_frames;

  // Placed at armRecordingStart()'s own snapshotted position, if this
  // take was ever armed - correct by construction, nothing to compute
  // here: wherever the transport genuinely was at record-start is where
  // this take starts, already resolved before this method ever runs. A
  // never-armed take stays unplaced - still a real, visible clip (Live
  // view/ArrangementGrid), just not an arrangement instance anywhere yet -
  // always true for a clip take, which never snapshots a start
  // position in the first place.
  if (recording_start_row_ >= 0) {
    int clip_index = reuse_existing ? take_it->second.clip_index : static_cast<int>(clips.size()) - 1;
    placeClipInstance(*song, track_id, recording_start_row_, clip_index);
  }
}

void
Controller::finishSampleCapture() {
  // hasRecordingClip() false means beginSampleCapture() was never called
  // this take at all - no audio ever actually arrived (no capture device
  // available, or stopped again before a first block landed) - so
  // there's nothing to finalize or clean up, the plain no-op this always
  // was before eager (at-start, rather than at-first-real-block) clip
  // creation was tried and dropped for showing nothing useful anyway.
  auto song = getCurrentSong();
  if (song && hasRecordingClip()) {
    auto track_id = getRecordingTrackId();
    auto clips = song->getClips(track_id);
    for (size_t i = 0; i < clips.size(); i++) {
      auto clip = clips[i];
      if (clip.getId() != recording_clip_id_) continue;

      // The layer beginSampleCapture() just finished writing into - always
      // the last one (its own append-only "overdub adds a new layer, a
      // fresh take reuses/creates layer 0" convention, and only one
      // recording is ever in flight at a time - Controller.h's own
      // armed_track_ids_ comment), never assumed to be layer 0, which for
      // an overdub is some *earlier*, already-finished take instead.
      Song::Edit edit(*song, "finish sample capture");
      auto content = clip.sampleLayer(clip.sampleLayerCount() - 1);
      auto total_frames = content.getBuffer() ? content.getBuffer()->numberOfFrames() : 0;
      // The lead-in trimmed off the front (recording_latency_frames_,
      // beginSampleCapture()'s own comment on what it is) was never real
      // content the performer could have produced - it shouldn't inflate
      // this instance's own arrangement-window length either.
      auto post_trim_frames = std::max(0, total_frames - recording_latency_frames_);
      auto measured_length = channel_config.framesToRows(post_trim_frames, song->getTempo());
      // An overdub only ever grows the clip's own length, never shrinks
      // it - the clip was already looping/playing at its own established
      // length for this to be an overdub at all, and a shorter new layer
      // just plays out its own tail-silence for the rest of each lap
      // rather than truncating every earlier layer's own loop point. A
      // fresh take has no earlier length to protect - its own starting
      // length was only ever a padded, ahead-of-time overshoot
      // (extendRecordingSampleClipIfNeeded()'s own per-bar growth), so it
      // always resolves to exactly the real measured length here.
      clip.setLength(clip.sampleLayerCount() > 1 ? std::max(clip.getLength(), measured_length) : measured_length);

      // Rebuilds getMixedContent()'s own cache now that this take's audio
      // is final - a no-op below getMixedContent()'s own >1-layer
      // threshold (a fresh, never-overdubbed take), otherwise the one real
      // write path that turns the layer just captured above into
      // something SampleTrackState::triggerClip() will actually play
      // mixed in with every earlier take.
      clip.rebuildMixedContent(channel_config.getAudioOutSampleRate(), song->getTempo());

      // Re-clears the instance's own reach now that its real length is
      // known - beginSampleCapture()'s own placeClipInstance() call had
      // to run before any audio existed to measure, so it could only
      // clear a single placeholder row, not this take's own actual span.
      // Anything genuinely stale still sitting later in the track's own
      // timeline (an old stop marker, a superseded instance) within reach
      // of what this take actually turned out to be was never cleared -
      // placeClipInstance() is idempotent against the same clip already
      // placed at the same row, so calling it again here with the correct
      // length simply extends that same clearing pass to cover it.
      if (recording_start_row_ >= 0) {
        placeClipInstance(*song, track_id, recording_start_row_, static_cast<int>(i));
      }

      auto duration_seconds = static_cast<float>(post_trim_frames) / static_cast<float>(channel_config.getAudioOutSampleRate());
      getUIEventQueue().push(make_unique<LogEvent>(fmt::format("recorded {} ({:.1f}s)", clip.getName(), duration_seconds)));
      break;
    }
  }

  recording_clip_id_.clear();
  recording_latency_frames_ = 0;
  // Back to unarmed - a later take that's never armed at all (nothing
  // currently reaches beginSampleCapture() that way, but stays a real,
  // defended case - see armRecordingStart()'s own comment) must not
  // silently inherit this one's now-stale position.
  recording_start_row_ = -1;
  stopRecording();
}

void
Controller::prewarmInstrumentForPreview(const Track * instrument, int note_value) const {
  if (!instrument) return;
  auto song = getCurrentSong();
  Tuning tuning = song ? song->getTuning() : Tuning::EDO31;
  prewarmInstrumentTree(*instrument, channel_config, tuning, note_value);
}
