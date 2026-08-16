#include "Controller.h"

#include "model/Song.h"
#include "model/InstrumentTrack.h"
#include "playback/PlaybackControlEvent.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>

#include <fmt/core.h>

using namespace std;

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
}

void
Controller::createNewSong() {
  auto song = make_shared<Song>();

  song->addTrack(make_unique<InstrumentTrack>(0));
  song->addScene();

  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    current_song = song;
  }
  current_song_filename = "song.xml";
  // The Player/audio thread holds a getSongPtr() copy of whatever
  // current_song used to point to (see that method's own comment on why
  // a plain reference/raw pointer isn't safe here); it must be told to
  // re-fetch before it drops that copy in favor of the one we just
  // reassigned above (see Player::play()'s SONG_CHANGED handling).
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
}

bool
Controller::openSong(const string & filename) {
  auto song = make_shared<Song>();
  if (!song->open(filename, instrument_provider)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> guard(song_mutex_);
    current_song = song;
  }
  current_song_filename = filename;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
  return true;
}

bool
Controller::sendCommand(std::string_view cmd) {
  if (cmd == "save-song") {
    current_song->save(current_song_filename);
  } else if (cmd == "add-filter") {

  } else if (cmd == "toggle-mixer-type") {
    // "Bypass HRTF entirely" toggle: AMBISONIC_STEREO <-> AMBISONIC_BINAURAL.
    // A no-op for a MONO config - MixerFactory never attempts binaural
    // decoding there regardless of this setting (see MixerFactory.cpp) -
    // but harmless to still flip, so no type check is needed here either.
    mixer_type_ = (mixer_type_ == MixerType::AMBISONIC_BINAURAL) ? MixerType::AMBISONIC_STEREO : MixerType::AMBISONIC_BINAURAL;
    fmt::print(stderr, "Mixer type set to {}\n", to_string(mixer_type_));
    getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MIXER_CHANGED));
  } else if (command_fallback_) {
    return command_fallback_(cmd);
  } else {
    return false;
  }
  return true;
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
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return false;
  instrument_track->setMuted(!instrument_track->isMuted());
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MUTED, track_id, instrument_track->isMuted() ? 1 : 0));
  return instrument_track->isMuted();
}

bool
Controller::toggleTrackSolo(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return false;
  instrument_track->setSolo(!instrument_track->isSolo());
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SOLO, track_id, instrument_track->isSolo() ? 1 : 0));
  return instrument_track->isSolo();
}

void
Controller::setTrackSendA(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setSendA(value);
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_A, track_id, static_cast<int>(value * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendB(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setSendB(value);
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_B, track_id, static_cast<int>(value * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendMain(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setSendMain(value);
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_MAIN, track_id, static_cast<int>(value * 1000.0f + 0.5f)));
}

void
Controller::setTrackAzimuth(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setAzimuth(value);
  current_song->incVersion();
  // Tenths-of-a-degree precision (-1800..1800) - the same "float via a
  // fixed-point int parameter" convention setTrackSendA/B use, just a
  // different scale/unit since this is degrees, not a 0-1 fraction.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_AZIMUTH, track_id, static_cast<int>(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f))));
}

void
Controller::addNoteColumn(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setMinNoteColumns(instrument_track->getMinNoteColumns() + 1);
  current_song->incVersion();
}

void
Controller::removeNoteColumn(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setMinNoteColumns(instrument_track->getMinNoteColumns() - 1);
  current_song->incVersion();
}

void
Controller::ensureRowCleared(std::set<std::pair<int, int>> & cleared_rows, int pattern_idx, int row, int track_id) {
  if (!cleared_rows.insert({row, track_id}).second) return; // already cleared this session
  auto & scene = current_song->getScene(pattern_idx);
  scene.setNotes(row, track_id, {});
  current_song->incVersion();
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
  auto & scene = current_song->getScene(pattern_idx);
  scene.setNote(row, track_id, note_column, Note(0, 0, delay));
  current_song->incVersion();
}

void
Controller::applyNotePressure(int pattern_idx, int row, int track_id, int note_column, short velocity, int delay) {
  auto & scene = current_song->getScene(pattern_idx);
  auto note = scene.getNote(row, track_id, note_column);
  if (!note.isDefined()) note.setDelay(delay);
  note.setVelocity(velocity);
  scene.setNote(row, track_id, note_column, note);
}
