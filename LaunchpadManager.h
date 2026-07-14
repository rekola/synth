#ifndef _LAUNCHPADMANAGER_H_
#define _LAUNCHPADMANAGER_H_

#include "Tuning.h"

#include <map>
#include <utility>
#include <vector>

class LaunchpadIO;
class Song;

// Owns everything about how connected Launchpads relate to the song being
// edited: per-device track assignment and octave, the isomorphic/
// percussion layout math needed to map a pad to a note and to an LED
// color (via LaunchpadLayout), and the active-note/redraw-row bookkeeping
// for chord-safe note entry. PatternEditor still performs the actual
// pattern edit (Song/Cursor/Pattern access stays its job) - this class
// owns Launchpad *layout*, keyed per physical device, not pattern
// editing. Musiceditor-only (depends on LaunchpadIO), like LaunchpadIO
// itself - the pure math it delegates to (LaunchpadLayout) stays in
// synth_engine and is unit-tested there.
class LaunchpadManager {
 public:
  void setLaunchpadIO(LaunchpadIO * io) { launchpad_io_ = io; }

  struct ActiveNote {
    int note_column;
    int row, track_id;
    int last_aftertouch_value = -1;
  };

  ActiveNote * findActiveNote(int device_id, int x, int y);
  void recordActiveNote(int device_id, int x, int y, ActiveNote note);
  void clearActiveNote(int device_id, int x, int y);
  bool hasAnyActiveNotes(int device_id) const;

  // Pattern row a Launchpad aftertouch/press write landed on when it isn't
  // the current playback row - see PatternEditor::render()'s incremental
  // redraw. A single shared value (not per-device): it names a row in the
  // shared pattern grid, not a per-device concept.
  int extraRedrawRow() const { return extra_redraw_row_; }
  void setExtraRedrawRow(int row) { extra_redraw_row_ = row; }
  void clearExtraRedrawRow() { extra_redraw_row_ = -1; }

  int octave(int device_id) const;
  void octaveUp(int device_id);
  void octaveDown(int device_id);

  // Returns the track *index* (into whatever track_ids vector the caller
  // is using, not a raw internal track id) this device should act on:
  // fallback_track_index if the device has never been explicitly
  // assigned one of its own.
  int assignedTrackIndex(int device_id, int fallback_track_index) const;

  // Advances this device's own assigned track index by delta (+1/-1),
  // seeding from fallback_track_index the first time (see
  // LaunchpadLayout::advanceTrackIndex) - the moment a device "detaches"
  // from following the shared fallback and starts tracking its own.
  void advanceTrack(int device_id, int delta, int fallback_track_index, int num_tracks);

  // Resolves the note this pad currently maps to for this device (using
  // its own octave and the given track's tuning/the song's key). Returns
  // -1 for an inactive percussion pad (row 7) or a degenerate/unpitched
  // tuning - callers should treat that as "ignore this press".
  int resolveNote(const Song & song, int device_id, int track_id, int x, int y) const;

  // Called once per render() frame: diffs each ready device's LED-relevant
  // state against its cache and pushes an LED refresh only where
  // something changed - the multi-device generalization of the old
  // single-device diff block. track_ids is whatever root-track-id list
  // the caller already computed (avoids this class needing to know how
  // to walk the track tree itself); fallback_track_index is used for any
  // device that hasn't been explicitly assigned a track of its own.
  void refresh(const Song & song, const std::vector<int> & track_ids, bool playing, int fallback_track_index);

 private:
  struct DeviceState {
    int assigned_track_id = -1; // index into track_ids, or -1 = unassigned
    int octave = 4;
    std::map<std::pair<int, int>, ActiveNote> active_notes;

    // LED diff cache.
    bool connected = false;
    Tuning tuning = Tuning::TET12;
    int key = -1;
    bool playing = false, muted = false, solo = false;
  };

  DeviceState & deviceState(int device_id);
  const DeviceState * findDeviceState(int device_id) const;
  void refreshLeds(int device_id, const DeviceState & state);

  LaunchpadIO * launchpad_io_ = nullptr;
  std::map<int, DeviceState> devices_;
  int extra_redraw_row_ = -1;
};

#endif
