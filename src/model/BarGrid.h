#ifndef _BARGRID_H_
#define _BARGRID_H_

#include "TimeSignature.h"

// The bars of one time signature counted from an origin row: bar 0 starts
// at `origin`, and rows before it carry the same bars backward.
struct BarGrid {
  TimeSignature signature{ 4, 4 };
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
  if (running.isActive() && row >= running.origin) return { running.signature, running.origin };
  return { song_signature, 0 };
}

#endif
