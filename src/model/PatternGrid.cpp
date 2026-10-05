#include "PatternGrid.h"
#include "Arrangement.h"
#include "Song.h"
#include "Clip.h"
#include "ArrangementOps.h"

#include <algorithm>
#include <set>
#include <string>

const Pattern *
ArrangementBackgroundGrid::find(int track_id, int row, int & pattern_row) const {
  auto & patterns = read_.getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) {
    pattern_row = row;
    return nullptr;
  }
  pattern_row = it->second.getEffectiveRow(row, 0);
  return &it->second;
}

Pattern *
ArrangementBackgroundGrid::find(int track_id, int row, int & pattern_row) {
  if (!write_) {
    pattern_row = row;
    return nullptr;
  }
  auto & patterns = write_->getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) {
    pattern_row = row;
    return nullptr;
  }
  pattern_row = it->second.getEffectiveRow(row, 0);
  return &it->second;
}

Pattern *
ArrangementBackgroundGrid::obtain(int track_id, int row, int & pattern_row) {
  if (!write_) {
    pattern_row = row;
    return nullptr;
  }
  // A track with no Pattern yet maps `row` unchanged, the same as reading.
  if (auto existing = find(track_id, row, pattern_row)) return existing;
  return &write_->getPatternsByTrack()[track_id];
}

const Clip *
SceneGrid::clipFor(int track_id) const {
  auto & clips = read_.getClips(track_id);
  if (scene_ < 0 || scene_ >= static_cast<int>(clips.size())) return nullptr;
  auto & clip = clips[static_cast<size_t>(scene_)];
  return clip.isEmpty() ? nullptr : &clip;
}

int
SceneGrid::clipRow(const Clip & clip, int row) {
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  if (row < 0 || (!clip.isLooping() && row >= length)) return -1;
  return clip.getLeafPattern().getEffectiveRow(row, length);
}

const Pattern *
SceneGrid::find(int track_id, int row, int & pattern_row) const {
  pattern_row = row;
  auto clip = clipFor(track_id);
  if (!clip || clip->hasSample()) return nullptr;
  auto clip_row = clipRow(*clip, row);
  if (clip_row < 0) return nullptr;
  pattern_row = clip_row;
  return &clip->getLeafPattern();
}

Pattern *
SceneGrid::find(int track_id, int row, int & pattern_row) {
  auto found = static_cast<const SceneGrid &>(*this).find(track_id, row, pattern_row);
  return write_ ? const_cast<Pattern *>(found) : nullptr;
}

Pattern *
SceneGrid::obtain(int track_id, int row, int & pattern_row) {
  pattern_row = row;
  if (!write_ || scene_ < 0 || row < 0) return nullptr;
  auto & clip = write_->ensureClipAt(track_id, scene_);
  if (clip.hasSample()) return nullptr;
  if (clip.getId().empty()) {
    clip.setId(write_->generateUniqueClipId());
    if (clip.getName().empty()) clip.setName("Clip " + std::to_string(scene_ + 1));
  }
  if (clip.getLength() <= 0) {
    clip.setLength(length_ > 0 ? length_ : 1);
    clip.setLooping(true);
  }
  if (!clip.isLooping() && row >= clip.getLength()) {
    auto rows_per_bar = write_->getSceneBarRows(scene_);
    clip.setLength((row / rows_per_bar + 1) * rows_per_bar);
  }
  pattern_row = clipRow(clip, row);
  return &clip.getLeafPattern();
}

ArrangementRegionGrid::ArrangementRegionGrid(Song & song, int anchor_row, std::string focused_clip_id)
  : read_song_(song), write_song_(&song), background_(song.getArrangement()),
    anchor_row_(anchor_row), focused_clip_id_(std::move(focused_clip_id)) { }

ArrangementRegionGrid::ArrangementRegionGrid(const Song & song, int anchor_row, std::string focused_clip_id)
  : read_song_(song), write_song_(nullptr), background_(song.getArrangement()),
    anchor_row_(anchor_row), focused_clip_id_(std::move(focused_clip_id)) { }

ArrangementRegionGrid::Source
ArrangementRegionGrid::sourceAt(int track_id, int row) const {
  auto & clips = read_song_.getClips(track_id);
  if (!focused_clip_id_.empty()) {
    for (size_t i = 0; i < clips.size(); i++) {
      if (clips[i].getId() == focused_clip_id_) return { SourceKind::FOCUSED, static_cast<int>(i), 0 };
    }
  }
  auto active = resolveInstanceAt(read_song_, track_id, row);
  if (active.clip_index >= 0) return { SourceKind::INSTANCE, active.clip_index, active.start_row };
  return {};
}

const Pattern *
ArrangementRegionGrid::clipPattern(int track_id, const Source & source, int row, int & pattern_row) const {
  pattern_row = row;
  if (source.kind == SourceKind::BACKGROUND) return nullptr;
  auto & clip = read_song_.getClips(track_id)[static_cast<size_t>(source.clip_index)];
  if (clip.hasSample()) return nullptr;
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  auto & pattern = clip.getLeafPattern();
  pattern_row = pattern.getEffectiveRow(row - source.start_row, length);
  return &pattern;
}

const Pattern *
ArrangementRegionGrid::find(int track_id, int row, int & pattern_row) const {
  pattern_row = row;
  auto source = sourceAt(track_id, anchor_row_);
  if (!(sourceAt(track_id, row) == source)) return nullptr;
  if (source.kind == SourceKind::BACKGROUND) return background_.find(track_id, row, pattern_row);
  return clipPattern(track_id, source, row, pattern_row);
}

Pattern *
ArrangementRegionGrid::find(int track_id, int row, int & pattern_row) {
  auto found = static_cast<const ArrangementRegionGrid &>(*this).find(track_id, row, pattern_row);
  return write_song_ ? const_cast<Pattern *>(found) : nullptr;
}

Pattern *
ArrangementRegionGrid::obtain(int track_id, int row, int & pattern_row) {
  pattern_row = row;
  if (!write_song_) return nullptr;
  auto source = sourceAt(track_id, anchor_row_);
  if (!(sourceAt(track_id, row) == source)) return nullptr;
  if (source.kind == SourceKind::BACKGROUND) return background_.obtain(track_id, row, pattern_row);
  return const_cast<Pattern *>(clipPattern(track_id, source, row, pattern_row));
}

std::pair<int, int>
ArrangementRegionGrid::sourceRows(int track_id) const {
  // A track's source can only change where an event is placed or a
  // one-shot clip ends.
  std::set<int> boundaries;
  auto & clips = read_song_.getClips(track_id);
  for (auto & [ row, clip_id ] : read_song_.getArrangement().getInstancesForTrack(track_id)) {
    boundaries.insert(row);
    for (auto & clip : clips) {
      if (clip.getId() == clip_id && !clip.isLooping()) boundaries.insert(row + std::max(clip.getLength(), 1));
    }
  }
  auto source = sourceAt(track_id, anchor_row_);
  int first = 0, last = Song::kMaxArrangementRows - 1;
  for (auto it = boundaries.upper_bound(anchor_row_); it != boundaries.end(); ++it) {
    if (!(sourceAt(track_id, *it) == source)) { last = *it - 1; break; }
  }
  for (auto it = boundaries.upper_bound(anchor_row_); it != boundaries.begin(); ) {
    --it;
    if (*it > 0 && !(sourceAt(track_id, *it - 1) == source)) { first = *it; break; }
  }
  return { first, last };
}
