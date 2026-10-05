#ifndef _BARGRID_H_
#define _BARGRID_H_

#include "TimeSignature.h"

#include <algorithm>
#include <map>
#include <vector>

// How the rows of a timeline fall into bars and beats: 4/4 from row 0,
// then whatever signature each marker sets from its row on. A marker that
// doesn't land on a bar start cuts the bar before it short. Rows before 0
// carry the first signature's bars backward.
class BarGrid {
 public:
  BarGrid() : segments_{ { 0, 16, 4, 0 } } { }

  // `markers`: the row each signature starts at; one at row 0 replaces 4/4.
  explicit BarGrid(const std::map<int, TimeSignature> & markers) {
    segments_.push_back({ 0, 16, 4, 0 });
    for (auto & [ row, signature ] : markers) {
      if (!signature.isSet() || row < 0) continue;
      if (row == 0) {
        segments_.front().bar_rows = signature.rowsPerBar();
        segments_.front().beat_rows = signature.rowsPerBeat();
        continue;
      }
      auto & previous = segments_.back();
      auto bars = (row - previous.start + previous.bar_rows - 1) / previous.bar_rows;
      segments_.push_back({ row, signature.rowsPerBar(), signature.rowsPerBeat(), previous.first_bar + bars });
    }
  }

  // The first row of the bar `row` is in.
  int barStart(int row) const {
    auto & segment = segmentAt(row);
    return segment.start + floorDiv(row - segment.start, segment.bar_rows) * segment.bar_rows;
  }
  int rowInBar(int row) const { return row - barStart(row); }
  // The rows per bar / per beat of the signature in force at `row`.
  int barRows(int row) const { return segmentAt(row).bar_rows; }
  int beatRows(int row) const { return segmentAt(row).beat_rows; }
  // The first bar start after `row`.
  int nextBarStart(int row) const {
    auto & segment = segmentAt(row);
    auto next = barStart(row) + segment.bar_rows;
    auto after = std::upper_bound(segments_.begin(), segments_.end(), row, [](int r, const Segment & s) { return r < s.start; });
    if (after != segments_.end() && next > after->start) next = after->start;
    return next;
  }
  // `row` itself if it starts a bar, else the next bar start.
  int roundUpToBar(int row) const { return barStart(row) == row ? row : nextBarStart(row); }
  // Bars are numbered from 0 at row 0.
  int barIndex(int row) const {
    auto & segment = segmentAt(row);
    return segment.first_bar + floorDiv(row - segment.start, segment.bar_rows);
  }
  int barStartRow(int bar) const {
    auto it = std::upper_bound(segments_.begin(), segments_.end(), bar, [](int b, const Segment & s) { return b < s.first_bar; });
    auto & segment = it == segments_.begin() ? segments_.front() : *(it - 1);
    return segment.start + (bar - segment.first_bar) * segment.bar_rows;
  }

 private:
  struct Segment {
    int start;
    int bar_rows;
    int beat_rows;
    int first_bar; // the number of the bar starting at `start`
  };

  static int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

  const Segment & segmentAt(int row) const {
    auto it = std::upper_bound(segments_.begin(), segments_.end(), row, [](int r, const Segment & s) { return r < s.start; });
    return it == segments_.begin() ? segments_.front() : *(it - 1);
  }

  std::vector<Segment> segments_;
};

#endif
