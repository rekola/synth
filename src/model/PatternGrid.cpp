#include "PatternGrid.h"
#include "Section.h"
#include "Song.h"
#include "Clip.h"
#include "ArrangementOps.h"

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

SectionRegionGrid::SectionRegionGrid(Song & song, Section & section, int anchor_row, std::string focused_clip_id)
  : read_song_(song), write_song_(&song), read_section_(section),
    background_(section, song.getEffectiveSectionLength(section)),
    anchor_row_(anchor_row), focused_clip_id_(std::move(focused_clip_id)) { }

SectionRegionGrid::SectionRegionGrid(const Song & song, const Section & section, int anchor_row, std::string focused_clip_id)
  : read_song_(song), write_song_(nullptr), read_section_(section),
    background_(section, song.getEffectiveSectionLength(section)),
    anchor_row_(anchor_row), focused_clip_id_(std::move(focused_clip_id)) { }

SectionRegionGrid::Source
SectionRegionGrid::sourceAt(int track_id, int row) const {
  auto & clips = read_song_.getClips(track_id);
  if (!focused_clip_id_.empty()) {
    for (size_t i = 0; i < clips.size(); i++) {
      if (clips[i].getId() == focused_clip_id_) return { SourceKind::FOCUSED, static_cast<int>(i), 0 };
    }
  }
  auto active = resolveInstanceAt(read_song_, read_section_, track_id, row);
  if (active.clip_index >= 0) return { SourceKind::INSTANCE, active.clip_index, active.start_row };
  return {};
}

const Pattern *
SectionRegionGrid::clipPattern(int track_id, const Source & source, int row, int & pattern_row) const {
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
SectionRegionGrid::find(int track_id, int row, int & pattern_row) const {
  pattern_row = row;
  auto source = sourceAt(track_id, anchor_row_);
  if (!(sourceAt(track_id, row) == source)) return nullptr;
  if (source.kind == SourceKind::BACKGROUND) return background_.find(track_id, row, pattern_row);
  return clipPattern(track_id, source, row, pattern_row);
}

Pattern *
SectionRegionGrid::find(int track_id, int row, int & pattern_row) {
  auto found = static_cast<const SectionRegionGrid &>(*this).find(track_id, row, pattern_row);
  return write_song_ ? const_cast<Pattern *>(found) : nullptr;
}

Pattern *
SectionRegionGrid::obtain(int track_id, int row, int & pattern_row) {
  pattern_row = row;
  if (!write_song_) return nullptr;
  auto source = sourceAt(track_id, anchor_row_);
  if (!(sourceAt(track_id, row) == source)) return nullptr;
  if (source.kind == SourceKind::BACKGROUND) return background_.obtain(track_id, row, pattern_row);
  return const_cast<Pattern *>(clipPattern(track_id, source, row, pattern_row));
}

std::pair<int, int>
SectionRegionGrid::sourceRows(int track_id) const {
  auto length = read_song_.getEffectiveSectionLength(read_section_);
  auto source = sourceAt(track_id, anchor_row_);
  int first = anchor_row_, last = anchor_row_;
  while (first > 0 && sourceAt(track_id, first - 1) == source) first--;
  while (last + 1 < length && sourceAt(track_id, last + 1) == source) last++;
  return { first, last };
}
