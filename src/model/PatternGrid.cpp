#include "PatternGrid.h"
#include "Section.h"
#include "Song.h"
#include "Clip.h"

#include <string>

const Pattern *
SectionBackgroundGrid::find(int track_id, int row, int & pattern_row) const {
  auto & patterns = read_.getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) {
    pattern_row = row;
    return nullptr;
  }
  pattern_row = it->second.getEffectiveRow(row, context_length_);
  return &it->second;
}

Pattern *
SectionBackgroundGrid::find(int track_id, int row, int & pattern_row) {
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
  pattern_row = it->second.getEffectiveRow(row, context_length_);
  return &it->second;
}

Pattern *
SectionBackgroundGrid::obtain(int track_id, int row, int & pattern_row) {
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
    auto rows_per_bar = write_->getRowsPerBar() > 0 ? write_->getRowsPerBar() : 1;
    clip.setLength((row / rows_per_bar + 1) * rows_per_bar);
  }
  pattern_row = clipRow(clip, row);
  return &clip.getLeafPattern();
}
