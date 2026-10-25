#ifndef _CHORDGROUPER_H_
#define _CHORDGROUPER_H_

#include "PatternSource.h"

#include <chrono>
#include <algorithm>
#include <optional>
#include <vector>

// Groups note keys typed together on a terminal that reports no key
// release: there is no telling a held chord from quick taps by hold state,
// but the keys of a chord arrive within a few milliseconds of each other.
// A key within kWindow of the first key of the open chord, on the same
// track, joins it - the chord's row, the next note column - instead of
// stepping to a row of its own. A note already in the chord never joins it
// again: a held key's repeats come faster than a chord is typed, and would
// stack into one chord with themselves.
class ChordGrouper {
 public:
  using Clock = std::chrono::steady_clock;

  static constexpr std::chrono::milliseconds kWindow{100};

  struct Slot {
    RowAddress row;
    int column;
  };

  // Where a key playing `note` at `when` goes if it joins the open chord,
  // else nullopt.
  std::optional<Slot> join(Clock::time_point when, int track_id, int note) {
    if (!open_ || track_id != track_id_ || when < start_ || when - start_ > kWindow) return std::nullopt;
    if (std::find(notes_.begin(), notes_.end(), note) != notes_.end()) return std::nullopt;
    notes_.push_back(note);
    return Slot{ row_, ++column_ };
  }

  // Opens a chord with its first key.
  void begin(Clock::time_point when, int track_id, RowAddress row, int column, int note) {
    notes_ = {note};
    open_ = true;
    start_ = when;
    track_id_ = track_id;
    row_ = row;
    column_ = column;
  }

  void reset() { open_ = false; }

 private:
  bool open_ = false;
  Clock::time_point start_;
  int track_id_ = 0;
  RowAddress row_;
  int column_ = 0;
  std::vector<int> notes_;
};

#endif
