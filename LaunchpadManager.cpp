#include "LaunchpadManager.h"

#include "LaunchpadIO.h"
#include "LaunchpadLayout.h"
#include "LaunchpadProtocol.h"
#include "Song.h"
#include "InstrumentTrack.h"

#include <algorithm>

using namespace std;

namespace {
  struct Rgb { uint8_t r, g, b; };
  // Fokker organ / Archiphone style landmark colors (see LaunchpadLayout::
  // PadCategory) - brightness carries the hierarchy: 127 for the diatonic
  // anchors, 70 in the blue channel for diesis (31-EDO's characteristic
  // in-between notes), 70 in red/amber/magenta for the dimmer, more
  // "ordinary" chromatic/accidental classes.
  constexpr Rgb FOKKER_TONIC      = {0,   127, 0};   // bright green - strongest landmark
  constexpr Rgb FOKKER_DIATONIC   = {127, 127, 127}; // bright white - other scale degrees
  constexpr Rgb FOKKER_SHARP      = {70,  0,   0};   // dim red
  constexpr Rgb FOKKER_FLAT       = {70,  35,  0};   // dim amber/orange
  constexpr Rgb FOKKER_DIESIS     = {0,   70,  127}; // medium blue
  constexpr Rgb FOKKER_ACCIDENTAL = {70,  0,   70};  // dim magenta - ambiguous tie (e.g. 12edo black keys)
}

LaunchpadManager::DeviceState &
LaunchpadManager::deviceState(int device_id) {
  return devices_[device_id];
}

const LaunchpadManager::DeviceState *
LaunchpadManager::findDeviceState(int device_id) const {
  auto it = devices_.find(device_id);
  return it == devices_.end() ? nullptr : &it->second;
}

LaunchpadManager::ActiveNote *
LaunchpadManager::findActiveNote(int device_id, int x, int y) {
  auto it = devices_.find(device_id);
  if (it == devices_.end()) return nullptr;
  auto note_it = it->second.active_notes.find({x, y});
  if (note_it == it->second.active_notes.end()) return nullptr;
  return &note_it->second;
}

void
LaunchpadManager::recordActiveNote(int device_id, int x, int y, ActiveNote note) {
  deviceState(device_id).active_notes[{x, y}] = note;
}

void
LaunchpadManager::clearActiveNote(int device_id, int x, int y) {
  auto it = devices_.find(device_id);
  if (it == devices_.end()) return;
  it->second.active_notes.erase({x, y});
}

bool
LaunchpadManager::hasAnyActiveNotes(int device_id) const {
  auto * state = findDeviceState(device_id);
  return state && !state->active_notes.empty();
}

int
LaunchpadManager::octave(int device_id) const {
  auto * state = findDeviceState(device_id);
  return state ? state->octave : 4;
}

void
LaunchpadManager::octaveUp(int device_id) {
  auto & state = deviceState(device_id);
  state.octave = LaunchpadLayout::clampOctave(state.octave, 1);
}

void
LaunchpadManager::octaveDown(int device_id) {
  auto & state = deviceState(device_id);
  state.octave = LaunchpadLayout::clampOctave(state.octave, -1);
}

int
LaunchpadManager::assignedTrackIndex(int device_id, int fallback_track_index) const {
  auto * state = findDeviceState(device_id);
  if (!state || state->assigned_track_id < 0) return fallback_track_index;
  return state->assigned_track_id;
}

void
LaunchpadManager::advanceTrack(int device_id, int delta, int fallback_track_index, int num_tracks) {
  auto & state = deviceState(device_id);
  auto current = state.assigned_track_id < 0 ? fallback_track_index : state.assigned_track_id;
  state.assigned_track_id = LaunchpadLayout::advanceTrackIndex(current, delta, num_tracks);
}

int
LaunchpadManager::resolveNote(const Song & song, int device_id, int track_id, int x, int y) const {
  auto track = song.getTrackByInternalId(track_id);
  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();

  if (tuning == Tuning::PERCUSSION) {
    return LaunchpadLayout::percussionNoteForPad(x, y);
  }

  auto edo_steps = LaunchpadLayout::edoSteps(tuning);
  if (edo_steps <= 0) return -1; // defensive - every non-percussion Tuning is currently pitched

  auto basis = LaunchpadLayout::computeBasis(edo_steps);
  auto tonic = song.getKey() >= 0 ? song.getKey() : 0;
  // Deliberately not "(octave - 4) * edo_steps" (which anchors pad (0,0) at
  // the raw tonic, an inaudibly low register for most instruments): the
  // computer-keyboard tables (InputEvent.h) each bake in their own
  // several-octaves-up baseline for their lowest key (TET12's 'z' is
  // base+48, i.e. exactly 4 octaves; TET31/TET53 use 5 octaves) -
  // multiplying by the octave directly, instead of recentering around 4,
  // reproduces that same baseline; +1 further octave on top of that per
  // user feedback (the "octave*N" register alone was still too low to be
  // comfortably useful on the Launchpad specifically).
  auto base_note = tonic + (octave(device_id) + 1) * edo_steps;
  return LaunchpadLayout::noteForPad(basis, x, y, base_note);
}

void
LaunchpadManager::refreshLeds(int device_id, const DeviceState & state) {
  vector<LaunchpadProtocol::PadColor> colors;

  if (state.tuning == Tuning::PERCUSSION) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        uint8_t r, g, b;
        switch (LaunchpadLayout::percussionFamilyForPad(x, y)) {
        case LaunchpadLayout::PercussionFamily::CORE:       r = 127; g = 0;   b = 0;   break;
        case LaunchpadLayout::PercussionFamily::HI_HAT:     r = 127; g = 127; b = 0;   break;
        case LaunchpadLayout::PercussionFamily::TOMS:       r = 127; g = 50;  b = 0;   break;
        case LaunchpadLayout::PercussionFamily::CYMBALS:    r = 100; g = 100; b = 127; break;
        case LaunchpadLayout::PercussionFamily::HAND_PERC:  r = 0;   g = 127; b = 0;   break;
        case LaunchpadLayout::PercussionFamily::LATIN:      r = 80;  g = 0;   b = 127; break;
        case LaunchpadLayout::PercussionFamily::WHISTLE:    r = 127; g = 0;   b = 80;  break;
        case LaunchpadLayout::PercussionFamily::ELECTRONIC: r = 40;  g = 40;  b = 40;  break;
        default:                                             r = 0;   g = 0;   b = 0;   break; // UNUSED (row 7)
        }
        colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), r, g, b});
      }
    }
  } else {
    auto edo_steps = LaunchpadLayout::edoSteps(state.tuning);
    if (edo_steps <= 0) {
      // Defensive - every non-percussion Tuning is currently pitched; blank
      // the grid rather than showing a stale/misleading layout if this
      // ever changes.
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), 0, 0, 0});
        }
      }
    } else {
      auto basis = LaunchpadLayout::computeBasis(edo_steps);
      auto tonic = state.key >= 0 ? state.key : 0;
      // Matches resolveNote's base_note (see its comment) - purely for
      // consistency, since classifyPad's result is invariant to a uniform
      // octave shift of base_note anyway.
      auto base_note = tonic + (state.octave + 1) * edo_steps;

      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          uint8_t r, g, b;
          if (basis.degenerate) {
            r = g = b = 40; // degraded/fallback visual mode - no meaningful scale structure
          } else {
            Rgb color;
            switch (LaunchpadLayout::classifyPad(basis, edo_steps, x, y, base_note)) {
            case LaunchpadLayout::PadCategory::TONIC:      color = FOKKER_TONIC;      break;
            case LaunchpadLayout::PadCategory::DIATONIC:   color = FOKKER_DIATONIC;   break;
            case LaunchpadLayout::PadCategory::SHARP:      color = FOKKER_SHARP;      break;
            case LaunchpadLayout::PadCategory::FLAT:       color = FOKKER_FLAT;       break;
            case LaunchpadLayout::PadCategory::DIESIS:     color = FOKKER_DIESIS;     break;
            case LaunchpadLayout::PadCategory::ACCIDENTAL: color = FOKKER_ACCIDENTAL; break;
            }
            r = color.r; g = color.g; b = color.b;
          }
          colors.push_back({LaunchpadProtocol::padToNoteNumber(x, y), r, g, b});
        }
      }
    }
  }

  // Extra-button LEDs (see the Launchpad follow-up plan's button table).
  // CC numbers unreachable on X/Mini MK3 (30, 20 - Pro MK3's left column)
  // are harmless to include here: those models simply don't have the
  // physical button, so the colourspec entry has nothing to light.
  colors.push_back({91, 30, 30, 30}); // octave-up, dim white (static)
  colors.push_back({92, 30, 30, 30}); // octave-down, dim white (static)
  colors.push_back({93, 0, 0, 60});   // prev-track, dim blue (static)
  colors.push_back({94, 0, 0, 60});   // next-track, dim blue (static)
  colors.push_back({95, 0, 0, 0});    // reserved
  colors.push_back({96, 0, 0, 0});    // reserved
  colors.push_back({97, 0, 0, 0});    // reserved
  colors.push_back({98, state.playing ? uint8_t(0) : uint8_t(20), state.playing ? uint8_t(127) : uint8_t(20), state.playing ? uint8_t(0) : uint8_t(20)}); // toggle-playing/record
  colors.push_back({99, 0, 0, 0});    // reserved - may be hardware-fixed (Clear/Delete)
  for (int cc = 19; cc <= 89; cc += 10) colors.push_back({cc, 0, 0, 0}); // right column, all reserved
  colors.push_back({30, state.muted ? uint8_t(127) : uint8_t(20), 0, 0}); // toggle-mute (Pro MK3 left column pos. 6)
  colors.push_back({20, state.solo ? uint8_t(127) : uint8_t(20), state.solo ? uint8_t(127) : uint8_t(20), 0}); // toggle-solo (Pro MK3 left column pos. 7)

  launchpad_io_->sendLeds(device_id, colors);
}

void
LaunchpadManager::refresh(const Song & song, const vector<int> & track_ids, bool playing, int fallback_track_index) {
  if (!launchpad_io_) return;
  auto ready_ids = launchpad_io_->readySessionIds();

  // Prune cached state for devices no longer connected - session ids are
  // never reused (see LaunchpadIO's next_session_id_), so a stale entry
  // can never become relevant again.
  for (auto it = devices_.begin(); it != devices_.end(); ) {
    bool still_ready = find(ready_ids.begin(), ready_ids.end(), it->first) != ready_ids.end();
    if (!still_ready) it = devices_.erase(it);
    else ++it;
  }

  auto num_tracks = static_cast<int>(track_ids.size());

  for (auto device_id : ready_ids) {
    auto & state = deviceState(device_id);

    auto track_index = assignedTrackIndex(device_id, fallback_track_index);
    if (track_index < 0 || track_index >= num_tracks) track_index = fallback_track_index;

    Tuning tuning = Tuning::TET12;
    int key_val = -1;
    bool muted = false, solo = false;
    if (track_index >= 0 && track_index < num_tracks) {
      auto track = song.getTrackByInternalId(track_ids[track_index]);
      tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
      key_val = song.getKey();
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
        auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
        muted = instrument_track.isMuted();
        solo = instrument_track.isSolo();
      }
    }

    bool became_connected = !state.connected;
    bool state_changed = tuning != state.tuning || key_val != state.key ||
      playing != state.playing || muted != state.muted || solo != state.solo;

    state.connected = true;
    state.tuning = tuning;
    state.key = key_val;
    state.playing = playing;
    state.muted = muted;
    state.solo = solo;

    if (became_connected || state_changed) refreshLeds(device_id, state);
  }
}
