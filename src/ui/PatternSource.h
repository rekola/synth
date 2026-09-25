#ifndef _PATTERNSOURCE_H_
#define _PATTERNSOURCE_H_

#include "../model/ArrangementOps.h"
#include "../model/VisibleTrackInfo.h"

#include <memory>
#include <optional>
#include <utility>
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
  // The track the cursor is on, for a source where each track keeps its
  // own position: the cursor is that track's, and every other track's
  // rows are shown relative to it.
  virtual void setCursorTrack(int) { }
  // Whether the cursor can't be moved right now - it follows a playhead.
  virtual bool cursorLocked() const { return false; }
  // The block `track_id` is showing at the cursor row, when tracks can be
  // in different ones; nullopt otherwise.
  virtual std::optional<int> trackBlock(int) const { return std::nullopt; }
  // Where `track_id`'s own position is shown, as a row of the editor - the
  // cursor row itself where every track shares one position.
  virtual RowAddress trackCursor(int) const { return cursor(); }
  // `address`, a row of the editor (the cursor track's), as `track_id`'s
  // own - the same row where every track shares one position.
  virtual RowAddress trackAddress(int, RowAddress address) const { return address; }

  // `row` rows past the start of `block`, carried across block boundaries.
  // A block of blockCount() or more means past the end.
  virtual RowAddress normalize(int block, int row) const = 0;
  virtual int blockCount() const = 0;
  virtual int blockLength(int block) const = 0;

  // Rows from `from` to `to`, negative when `to` comes first.
  int rowsBetween(RowAddress from, RowAddress to) const {
    if (to.block < from.block) return -rowsBetween(to, from);
    int rows = to.row - from.row;
    for (auto block = from.block; block < to.block; block++) rows += blockLength(block);
    return rows;
  }
  // `address` moved by `rows`, carried across blocks. Rows before the
  // first block stay negative rows of block 0.
  RowAddress advance(RowAddress address, int rows) const {
    address.row += rows;
    while (address.row < 0 && address.block > 0) {
      address.block--;
      address.row += blockLength(address.block);
    }
    return address.row < 0 ? address : normalize(address.block, address.row);
  }

  // Note content: what's shown at, and what an edit writes to, a cell.
  virtual ReadTarget read(int track_id, RowAddress address) const = 0;
  virtual EditTarget edit(int track_id, RowAddress address) = 0;
  // Effect commands and block operations (kill/copy/yank/transpose) in
  // `anchor`'s block. `anchor` picks, per track, which content a block
  // operation acts on - whatever supplies that track at the anchor row
  // (see sourceRows()). `create`: whether writing may create the block's
  // storage (entry, paste) or only change what already exists (clearing).
  virtual std::unique_ptr<const PatternGrid> readGrid(RowAddress anchor) const = 0;
  virtual std::unique_ptr<PatternGrid> editGrid(RowAddress anchor, bool create) = 0;
  // The rows [first, last] of `anchor`'s block over which `track_id`'s
  // notes come from the same content as at the anchor row - a selection
  // never spans more than that.
  virtual std::pair<int, int> sourceRows(int track_id, RowAddress anchor) const = 0;

  // Widens `track_info` for every track's content in the `rows` rows
  // starting `first.row` rows into `first.block`.
  virtual void collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const = 0;

  // The song's own row (Song::getLocators()' key) at `address`, where
  // this source shows locators; nullopt where it doesn't.
  virtual std::optional<int> locatorRow(RowAddress address) const = 0;

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

  // The row `track_id` is playing in `block`, when that differs per track
  // (clips launched independently); nullopt otherwise.
  virtual std::optional<int> playheadRow(int track_id, int block) const = 0;

  // Whether a cell whose notes come from a clip (ReadTarget::is_instance)
  // is drawn tinted, marking that they don't live where they're shown.
  virtual bool showsClipIndirection() const = 0;
  virtual bool hasAnnotations() const = 0;
  // Whether the cursor row is the transport's position: it follows
  // playback, can't be moved while playing, and live recording writes
  // there.
  virtual bool cursorFollowsTransport() const = 0;
};

#endif
