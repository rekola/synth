#include "ScenePatternSource.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/Clip.h"
#include "../model/PatternGrid.h"

#include <algorithm>

namespace {

// A region's rows, each track's resolved through its own position (see
// ScenePatternSource's own comment): row r of the anchor's scene is, for
// a track, that track's row as far from its own position as r is from the
// cursor. Built over a const Song, it's read-only.
class PositionedSceneGrid : public PatternGrid {
 public:
  PositionedSceneGrid(const ScenePatternSource & source, Song & song, int anchor_block)
    : source_(source), read_(song), write_(&song), anchor_block_(anchor_block) { }
  PositionedSceneGrid(const ScenePatternSource & source, const Song & song, int anchor_block)
    : source_(source), read_(song), write_(nullptr), anchor_block_(anchor_block) { }

  const Pattern * find(int track_id, int row, int & pattern_row) const override {
    pattern_row = row;
    auto address = trackRow(track_id, row);
    if (!address) return nullptr;
    const SceneGrid grid(read_, address->block, source_.blockLength(address->block));
    return grid.find(track_id, address->row, pattern_row);
  }
  Pattern * find(int track_id, int row, int & pattern_row) override {
    auto found = static_cast<const PositionedSceneGrid &>(*this).find(track_id, row, pattern_row);
    return write_ ? const_cast<Pattern *>(found) : nullptr;
  }
  Pattern * obtain(int track_id, int row, int & pattern_row) override {
    pattern_row = row;
    auto address = trackRow(track_id, row);
    if (!write_ || !address) return nullptr;
    return SceneGrid(*write_, address->block, source_.blockLength(address->block)).obtain(track_id, address->row, pattern_row);
  }

 private:
  std::optional<RowAddress> trackRow(int track_id, int row) const {
    auto address = source_.trackAddress(track_id, { anchor_block_, row });
    if (address.block < 0 || address.block >= source_.blockCount()) return std::nullopt;
    return address;
  }

  const ScenePatternSource & source_;
  const Song & read_;
  Song * write_;
  int anchor_block_;
};

}

Song &
ScenePatternSource::song() const {
  return controller_.getSong();
}

int
ScenePatternSource::sceneCount(const Song & song) {
  return std::max(8, song.getUsedSceneCount() + 1);
}

int
ScenePatternSource::blockCount() const {
  return sceneCount(song());
}

int
ScenePatternSource::blockLength(int block) const {
  const Song & s = song();
  SceneGrid grid(s, block, 0);
  int length = 0;
  for (auto track_id : s.getRootTrackIds()) {
    if (auto clip = grid.clipFor(track_id)) length = std::max(length, clip->getLength());
  }
  if (length > 0) return length;
  return s.getRowsPerBar() > 0 ? s.getRowsPerBar() : 1;
}

RowAddress
ScenePatternSource::clamp(RowAddress address) const {
  address.block = std::clamp(address.block, 0, blockCount() - 1);
  address.row = std::clamp(address.row, 0, blockLength(address.block) - 1);
  return address;
}

std::unordered_map<int, RowAddress> &
ScenePatternSource::positions() const {
  return positions_[controller_.getActiveBufferName()];
}

bool
ScenePatternSource::isPlaying(int track_id) const {
  auto it = playheads_.find(track_id);
  return it != playheads_.end() && it->second.row >= 0;
}

RowAddress
ScenePatternSource::position(int track_id) const {
  if (isPlaying(track_id)) {
    auto & playhead = playheads_.at(track_id);
    return clamp({ playhead.scene, playhead.row });
  }
  return clamp(positions()[track_id]);
}

int
ScenePatternSource::offset(int track_id) const {
  auto it = offsets_.find(track_id);
  return track_id != cursor_track_id_ && it != offsets_.end() && isPlaying(track_id) ? it->second : 0;
}

void
ScenePatternSource::setPlayheads(std::unordered_map<int, Playhead> playheads) {
  // A track that stops stays as it's shown: its position is the row at the
  // cursor row.
  for (auto & [ track_id, playhead ] : playheads_) {
    auto it = playheads.find(track_id);
    if (playhead.row < 0 || (it != playheads.end() && it->second.row >= 0)) continue;
    auto left = advance({ playhead.scene, playhead.row }, -offset(track_id));
    positions()[track_id] = left.row < 0 ? RowAddress{ 0, 0 } : clamp(left);
    offsets_.erase(track_id);
  }
  playheads_ = std::move(playheads);
}

void
ScenePatternSource::setCursorTrack(int track_id) {
  if (track_id == cursor_track_id_) return;
  // The cursor row moves to where the new track's position is shown, so
  // every playing track's line is now that much nearer to it.
  auto shift = offset(track_id);
  auto old_cursor_track_id = cursor_track_id_;
  cursor_track_id_ = track_id;
  for (auto & [ playing_id, playhead ] : playheads_) {
    if (playhead.row < 0 || playing_id == track_id) continue;
    offsets_[playing_id] = (playing_id == old_cursor_track_id ? 0 : offsets_[playing_id]) - shift;
  }
  offsets_.erase(track_id);
  // The stopped tracks move along with the cursor row, as they do when it
  // moves - the old cursor track among them.
  moveStoppedTracks(shift);
}

bool
ScenePatternSource::keepPlayheadsVisible(RowAddress top, int rows, int margin) {
  bool moved = false;
  for (auto & [ track_id, playhead ] : playheads_) {
    if (playhead.row < 0 || track_id == cursor_track_id_) continue;
    auto line = rowsBetween(top, trackCursor(track_id));
    auto kept = std::clamp(line, margin, std::max(margin, rows - 1 - margin));
    if (kept == line) continue;
    offsets_[track_id] = offset(track_id) + kept - line;
    moved = true;
  }
  return moved;
}

void
ScenePatternSource::moveStoppedTracks(int rows) {
  if (rows == 0) return;
  for (auto track_id : song().getRootTrackIds()) {
    if (track_id == cursor_track_id_ || isPlaying(track_id)) continue;
    auto moved = advance(position(track_id), rows);
    if (moved.row < 0) moved = { 0, 0 };
    positions()[track_id] = clamp(moved);
  }
}

RowAddress
ScenePatternSource::cursor() const {
  return position(cursor_track_id_);
}

void
ScenePatternSource::moveCursor(int delta_rows) {
  if (cursorLocked()) return;
  auto old_cursor = cursor();
  auto address = advance(old_cursor, delta_rows);
  if (address.row < 0) address = { 0, 0 };
  if (address.block >= blockCount()) address = { blockCount() - 1, blockLength(blockCount() - 1) - 1 };
  positions()[cursor_track_id_] = address;
  auto moved = rowsBetween(old_cursor, address);
  moveStoppedTracks(moved);
  // Playing tracks' lines stay put as the cursor row moves away.
  for (auto & [ track_id, playhead ] : playheads_) {
    if (playhead.row >= 0 && track_id != cursor_track_id_) offsets_[track_id] = offset(track_id) - moved;
  }
}

void
ScenePatternSource::setTrackPosition(int track_id, RowAddress address) {
  if (isPlaying(track_id)) return;
  positions()[track_id] = clamp(address);
}

RowAddress
ScenePatternSource::normalize(int block, int row) const {
  auto count = blockCount();
  while (block < count && row >= blockLength(block)) {
    row -= blockLength(block);
    block++;
  }
  return { block, row };
}

RowAddress
ScenePatternSource::trackAddress(int track_id, RowAddress address) const {
  if (track_id == cursor_track_id_) return address;
  return advance(position(track_id), rowsBetween(cursor(), address) - offset(track_id));
}

ReadTarget
ScenePatternSource::read(int track_id, RowAddress address) const {
  static const Pattern empty_pattern;
  const Song & s = song();
  address = trackAddress(track_id, address);
  // Rows before the first scene show nothing.
  if (address.row < 0) return { &empty_pattern, 0, address.row, false, -1 };
  SceneGrid grid(s, address.block, 0);
  auto clip = grid.clipFor(track_id);
  if (!clip) return { &empty_pattern, 0, address.row, false, -1 };
  auto length = clip->getLength() > 0 ? clip->getLength() : 1;
  // A sample clip has no Pattern, but the waveform drawing still needs to
  // know which clip this is.
  if (clip->hasSample()) return { &empty_pattern, 0, address.row, true, address.block, false, length };
  auto clip_row = SceneGrid::clipRow(*clip, address.row);
  if (clip_row < 0) return { &empty_pattern, 0, address.row, false, -1 };
  return { &clip->getLeafPattern(), clip_row, address.row, true, address.block, false, length };
}

EditTarget
ScenePatternSource::edit(int track_id, RowAddress address) {
  address = trackAddress(track_id, address);
  int pattern_row;
  if (address.block >= 0 && address.block < blockCount() && address.row >= 0) {
    SceneGrid grid(song(), address.block, blockLength(address.block));
    if (auto pattern = grid.obtain(track_id, address.row, pattern_row)) return { pattern, pattern_row };
  }
  // A slot holding sample audio has no Pattern to write notes into (and no
  // note columns to type them in), and there's no slot before the first
  // scene - writes land nowhere.
  static Pattern discarded;
  discarded = Pattern();
  return { &discarded, 0 };
}

std::unique_ptr<const PatternGrid>
ScenePatternSource::readGrid(RowAddress anchor) const {
  const Song & s = song();
  return std::make_unique<PositionedSceneGrid>(*this, s, anchor.block);
}

std::unique_ptr<PatternGrid>
ScenePatternSource::editGrid(RowAddress anchor, bool) {
  return std::make_unique<PositionedSceneGrid>(*this, song(), anchor.block);
}

void
ScenePatternSource::collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const {
  const Song & s = song();
  auto count = blockCount();
  for (auto track_id : s.getRootTrackIds()) {
    auto address = trackAddress(track_id, normalize(first.block, first.row));
    // Rows before the first scene show nothing; scanning from its start
    // instead only ever widens a column a little more than needed.
    if (address.block < 0) address = { 0, 0 };
    for (int covered = 0; covered < rows && address.block < count; address = { address.block + 1, 0 }) {
      auto clip = SceneGrid(s, address.block, 0).clipFor(track_id);
      if (clip && !clip->hasSample()) clip->getLeafPattern().updateSubtrackInfo(track_info[track_id]);
      covered += blockLength(address.block) - address.row;
    }
  }
}

void
ScenePatternSource::insertRow(int track_id, RowAddress address) {
  address = trackAddress(track_id, address);
  if (address.row < 0) return;
  SceneGrid grid(song(), address.block, 0);
  auto clip = grid.clipFor(track_id);
  int pattern_row;
  if (auto pattern = grid.find(track_id, address.row, pattern_row)) {
    pattern->insertRow(pattern_row, clip->getLength() > 0 ? clip->getLength() : 1);
  }
}

std::optional<int>
ScenePatternSource::playheadRow(int track_id, int block) const {
  if (!isPlaying(track_id)) return std::nullopt;
  auto at = trackCursor(track_id);
  if (at.block != block) return std::nullopt;
  return at.row;
}
