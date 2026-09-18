#ifndef _PATTERNSOURCE_H_
#define _PATTERNSOURCE_H_

#include "../model/ArrangementOps.h"
#include "../model/VisibleTrackInfo.h"

#include <memory>
#include <unordered_map>

class PatternGrid;
class Section;
class SampleContent;

// A position in a pattern editor's row space: `row` rows into `block` (a
// section, in arrangement mode).
struct RowAddress {
  int block = 0;
  int row = 0;
  bool operator==(const RowAddress & other) const { return block == other.block && row == other.row; }
  bool operator!=(const RowAddress & other) const { return !(*this == other); }
};

// Where a pattern editor's rows and cells come from, and where its edits
// go - everything that differs between editing the arrangement and editing
// clips directly. Toolkit-agnostic, so any UI backend's pattern editor can
// share it.
class PatternSource {
 public:
  virtual ~PatternSource() = default;

  // The row half of the edit cursor (the column half is always the
  // current track).
  virtual RowAddress cursor() const = 0;
  virtual void moveCursor(int delta_rows) = 0;

  // `row` rows past the start of `block`, carried across block boundaries.
  // A block of blockCount() or more means past the end.
  virtual RowAddress normalize(int block, int row) const = 0;
  virtual int blockCount() const = 0;
  virtual int blockLength(int block) const = 0;

  // Note content: what's shown at, and what an edit writes to, a cell.
  virtual ReadTarget read(int track_id, RowAddress address) const = 0;
  virtual EditTarget edit(int track_id, RowAddress address) = 0;
  // Effect commands and block operations (kill/copy/yank/transpose) for
  // `block`. `create`: whether writing may create the block's storage
  // (entry, paste) or only change what already exists (clearing).
  virtual std::unique_ptr<const PatternGrid> readGrid(int block) const = 0;
  virtual std::unique_ptr<PatternGrid> editGrid(int block, bool create) = 0;

  // Widens `track_info` for every track's content in the `rows` rows
  // starting `first.row` rows into `first.block`.
  virtual void collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const = 0;

  // Row annotations for `block`, or nullptr when this source has none.
  // `create` as for editGrid().
  virtual const Section * annotations(int block) const = 0;
  virtual Section * annotations(int block, bool create) = 0;

  // Shifts one track's rows down from `address`, within its block.
  virtual void insertRow(int track_id, RowAddress address) = 0;
  // Whether a placed clip is playing on `track_id` at `address` - drawn
  // with the clip-indirection tint.
  virtual bool hasInstance(int track_id, RowAddress address) const = 0;
  // Places an explicit stop on `track_id` at `address` when any instance
  // event is in effect there. Returns false (doing nothing) when none is.
  virtual bool stopInstance(int track_id, RowAddress address) = 0;
  // Audio a sample track plays at `block` with no clip placed.
  virtual const SampleContent * sampleBackground(int track_id, int block) const = 0;
};

#endif
