#ifndef _MIDINOTEINPUT_H_
#define _MIDINOTEINPUT_H_

#include "MidiEvent.h"
#include "../model/ArrangementOps.h"

#include <functional>
#include <unordered_map>
#include <vector>

class Controller;

// Turns incoming MIDI into live notes on one track, independent of any UI:
// sounds them (when the track is monitored), gives each held note its own
// voice slot (note column), and optionally writes them into the pattern the
// caller resolves. What a UI calls its cursor is just a track plus a
// position; the caller supplies both, so the same code serves the terminal
// UI (cursor row) and headless mode (playhead).
class MidiNoteInput {
 public:
  struct Options {
    // Store notes in the pattern as well as playing them.
    bool write = false;
    // Note pressure is also recorded as automation at the transport's row
    // (only meaningful where the edit position follows the transport).
    bool pressure_follows_transport = false;
  };
  // Where a note for `track_id` lands now.
  using TargetResolver = std::function<EditTarget(int track_id)>;

  // Returns true if the song's content changed.
  bool handle(const MidiEvent & ev, Controller & controller, int track_id, const Options & options, const TargetResolver & resolve);

  // Tracks that have a note held right now.
  std::vector<int> heldTrackIds() const;
  bool hasHeldNotes() const { return !held_.empty(); }

 private:
  struct Held {
    int column;
    int track_id;
  };
  // Lowest voice slot on `track_id` no held note uses.
  int freeColumn(int track_id) const;

  // Keyed by MIDI note number.
  std::unordered_map<int, Held> held_;
};

#endif
