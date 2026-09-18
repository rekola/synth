#include "PatternGrid.h"
#include "Section.h"

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
