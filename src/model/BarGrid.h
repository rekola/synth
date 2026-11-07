#ifndef _BARGRID_H_
#define _BARGRID_H_

#include "TimeSignature.h"

#include <algorithm>
#include <map>
#include <vector>

// The bars of one time signature counted from an origin row: bar 0 starts
// at `origin`, and rows before it carry the same bars backward.
struct BarGrid {
  TimeSignature signature{4, 4};
  int origin = 0;

  int barRows() const { return signature.rowsPerBar(); }
  int beatRows() const { return signature.rowsPerBeat(); }
  // The first row of the bar `row` is in.
  int barStart(int row) const { return origin + floorDiv(row - origin, barRows()) * barRows(); }
  int rowInBar(int row) const { return row - barStart(row); }
  // The first bar start after `row`.
  int nextBarStart(int row) const { return barStart(row) + barRows(); }
  // `row` itself if it starts a bar, else the next bar start.
  int roundUpToBar(int row) const { return barStart(row) == row ? row : nextBarStart(row); }
  // Bars are numbered from 0 at the origin.
  int barIndex(int row) const { return floorDiv(row - origin, barRows()); }
  int barStartRow(int bar) const { return origin + bar * barRows(); }

 private:
  static int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
};

// The arrangement's bars: the song's signature counted from row 0, then
// each signature change counting its own bars from its row. A change
// normally sits on a bar start; one that does not cuts the bar before it
// short. Bars are numbered from 0 straight across the changes.
class ArrangementBars {
 public:
  ArrangementBars(TimeSignature initial, const std::map<int, TimeSignature> & changes) {
    segments_.push_back({0, initial, 0});
    for (auto & [row, signature] : changes) {
      if (row < 0 || !signature.isSet()) continue;
      auto & last = segments_.back();
      if (row == 0) {
        last.signature = signature;
        continue;
      }
      auto rows = last.signature.rowsPerBar();
      segments_.push_back({row, signature, last.first_bar + (row - last.origin + rows - 1) / rows});
    }
  }

  // The bars in force at `row`.
  BarGrid gridAt(int row) const {
    auto & s = segmentAt(row);
    return {s.signature, s.origin};
  }
  int barIndex(int row) const {
    auto & s = segmentAt(row);
    return s.first_bar + BarGrid{s.signature, s.origin}.barIndex(row);
  }
  // The first row of bar `bar`.
  int barStartRow(int bar) const {
    auto it = std::upper_bound(segments_.begin(), segments_.end(), bar, [](int b, const Segment & s) { return b < s.first_bar; });
    auto & s = it == segments_.begin() ? segments_.front() : *(it - 1);
    return s.origin + (bar - s.first_bar) * s.signature.rowsPerBar();
  }
  int barStart(int row) const { return gridAt(row).barStart(row); }
  int rowInBar(int row) const { return gridAt(row).rowInBar(row); }
  int barRows(int row) const { return gridAt(row).barRows(); }
  int beatRows(int row) const { return gridAt(row).beatRows(); }
  // The first bar start after `row`, or the next change if that comes first.
  int nextBarStart(int row) const {
    auto next = gridAt(row).nextBarStart(row);
    for (auto & s : segments_)
      if (s.origin > row) return std::min(next, s.origin);
    return next;
  }
  int roundUpToBar(int row) const { return barStart(row) == row ? row : nextBarStart(row); }
  bool hasChanges() const { return segments_.size() > 1; }

 private:
  struct Segment {
    int origin;
    TimeSignature signature;
    int first_bar;
  };
  const Segment & segmentAt(int row) const {
    auto it = std::upper_bound(segments_.begin(), segments_.end(), row, [](int r, const Segment & s) { return r < s.origin; });
    return it == segments_.begin() ? segments_.front() : *(it - 1);
  }
  std::vector<Segment> segments_;
};

// The signature a launched scene set, counted from the bar it launched on;
// unset (numerator 0) while the song's own signature applies.
struct RunningBars {
  TimeSignature signature;
  int origin = 0;
  bool isActive() const { return signature.isSet(); }
};

// The bars in force at `row`: the running ones from their origin on, else
// the song's (counted from row 0).
inline BarGrid
barsAt(const TimeSignature & song_signature, const RunningBars & running, int row) {
  if (running.isActive() && row >= running.origin) return {running.signature, running.origin};
  return {song_signature, 0};
}

// The same with the arrangement's own signature changes: the running bars
// win from their origin on, else the arrangement's bars at `row`.
inline BarGrid
barsAt(const ArrangementBars & arrangement, const RunningBars & running, int row) {
  if (running.isActive() && row >= running.origin) return {running.signature, running.origin};
  return arrangement.gridAt(row);
}

#endif
