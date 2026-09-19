#include "ScenePatternSource.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/Clip.h"
#include "../model/PatternGrid.h"

#include <algorithm>

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

RowAddress
ScenePatternSource::cursor() const {
  return clamp(cursors_[controller_.getActiveBufferName()]);
}

void
ScenePatternSource::moveCursor(int delta_rows) {
  auto address = cursor();
  address.row += delta_rows;
  while (address.row < 0 && address.block > 0) {
    address.block--;
    address.row += blockLength(address.block);
  }
  address = normalize(address.block, std::max(address.row, 0));
  if (address.block >= blockCount()) address = { blockCount() - 1, blockLength(blockCount() - 1) - 1 };
  cursors_[controller_.getActiveBufferName()] = address;
}

void
ScenePatternSource::setCursor(RowAddress address) {
  cursors_[controller_.getActiveBufferName()] = clamp(address);
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

ReadTarget
ScenePatternSource::read(int track_id, RowAddress address) const {
  static const Pattern empty_pattern;
  const Song & s = song();
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
  SceneGrid grid(song(), address.block, blockLength(address.block));
  int pattern_row;
  if (auto pattern = grid.obtain(track_id, address.row, pattern_row)) return { pattern, pattern_row };
  // A slot holding sample audio has no Pattern to write notes into (and no
  // note columns to type them in) - writes land nowhere.
  static Pattern discarded;
  discarded = Pattern();
  return { &discarded, 0 };
}

std::unique_ptr<const PatternGrid>
ScenePatternSource::readGrid(int block) const {
  const Song & s = song();
  return std::make_unique<SceneGrid>(s, block, blockLength(block));
}

std::unique_ptr<PatternGrid>
ScenePatternSource::editGrid(int block, bool) {
  return std::make_unique<SceneGrid>(song(), block, blockLength(block));
}

void
ScenePatternSource::collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const {
  const Song & s = song();
  auto address = normalize(first.block, first.row);
  auto count = blockCount();
  auto track_ids = s.getRootTrackIds();
  for (int covered = 0; covered < rows && address.block < count; address = { address.block + 1, 0 }) {
    SceneGrid grid(s, address.block, 0);
    for (auto track_id : track_ids) {
      auto clip = grid.clipFor(track_id);
      if (clip && !clip->hasSample()) clip->getLeafPattern().updateSubtrackInfo(track_info[track_id]);
    }
    covered += blockLength(address.block) - address.row;
  }
}

void
ScenePatternSource::insertRow(int track_id, RowAddress address) {
  SceneGrid grid(song(), address.block, 0);
  auto clip = grid.clipFor(track_id);
  int pattern_row;
  if (auto pattern = grid.find(track_id, address.row, pattern_row)) {
    pattern->insertRow(pattern_row, clip->getLength() > 0 ? clip->getLength() : 1);
  }
}

std::optional<int>
ScenePatternSource::playheadRow(int track_id, int block) const {
  auto it = playheads_.find(track_id);
  if (it == playheads_.end() || it->second.scene != block || it->second.row < 0) return std::nullopt;
  return it->second.row;
}
