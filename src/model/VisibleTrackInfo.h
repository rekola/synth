#ifndef _VISIBLETRACKINFO_H_
#define _VISIBLETRACKINFO_H_

#include "Color.h"

#include <cmath>
#include <utility>

enum class ColumnType {
  UNKNOWN = 0,
  NOTE,
  VELOCITY,
  DELAY,
  EFFECT
};

class VisibleTrackInfo {
public:
  VisibleTrackInfo() { }

  // A collapsed track is always exactly 1 column, full stop - regardless
  // of how many real note/velocity/delay/effect columns, or subtrack
  // chords, it actually has (see getColumnWidth()'s own comment on why
  // per-column content is hidden while collapsed). Without this, a
  // collapsed track with N real columns still rendered as N separate
  // 1-character-wide columns - N cells of clutter instead of the single
  // placeholder cell collapsing is meant to shrink it down to.
  int getColumnCount() const {
    if (collapsed_) return 1;
    return num_subtracks_ * ((has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0)) + (has_effect_column_ ? 1 : 0);
  }
  // The track's true total on-screen footprint, *including* its own
  // trailing "|" border character (see getColumnWidth()'s own comment for
  // why summing getColumnWidth() over every column gets this exactly
  // right, not by one character either way).
  int getTrackWidth() const {
    int w = 0;
    for (int k = 0; k < getColumnCount(); k++) w += getColumnWidth(k);
    return w;
  }
  // Column k's own true content width (3/2/2/4 - matches renderRow()'s own
  // `current_pos +=` increments exactly) plus a flat +1. That +1 is *not*
  // a per-column separator (a column doesn't know whether it's about to be
  // the first one actually drawn in some sub-range, which is the only
  // thing that ever determines whether a real separator precedes it) - it's
  // this column's own share of the track's total border/separator budget:
  // N columns need N-1 real inter-column separators plus 1 trailing "|",
  // i.e. N extra characters total, and N columns each contributing +1 adds
  // up to exactly that. This only balances out over a **complete** track
  // (0 through getColumnCount() - 1) - a caller summing a *sub-range* that
  // doesn't reach the track's real last column (PatternScroll.cpp's
  // trackWidthRange(), used for a column-scrolled anchor track or a
  // deliberately truncated terminal track) needs to give back one of those
  // reserved characters, since no trailing border is actually drawn there.
  int getColumnWidth(int k) const {
    // A collapsed track hides every column's own content (see
    // PatternEditor::renderRow) - collapsed_content_width_ blank
    // content cells plus its own trailing "│" border, instead of the
    // type's normal content width + 1. An instrument/percussion/
    // arpeggiator track (see SongStructure.cpp) gets 1 blank cell, so
    // its "▸"/"◂" heading toggle has an actual cell of its own to sit
    // in rather than the border being the track's *entire* on-screen
    // footprint; an effect track has no such toggle at this level (its
    // own ancestor-row box carries it instead - see
    // PatternEditor::renderHeading) and stays border-only.
    if (collapsed_) return 1 + collapsed_content_width_;
    switch (getColumnType(k)) {
    case ColumnType::NOTE: return 4;
    case ColumnType::VELOCITY: return 3;
    case ColumnType::DELAY: return 3;
    case ColumnType::EFFECT: return 5;
    default: return 0;
    }
  }
  ColumnType getColumnType(int k) const {
    auto column_count = getColumnCount();
    if (has_effect_column_ && k == column_count - 1) {
      return ColumnType::EFFECT;
    } else {
      auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
      k = k % n;

      if (has_note_column_) {
	if (k == 0) return ColumnType::NOTE;
	else k--;
      }

      if (k < num_velocity_columns_) return ColumnType::VELOCITY;
      else k -= num_velocity_columns_;

      if (has_delay_column_) {
	if (k == 0) return ColumnType::DELAY;
	else k--;      
      }
      
      return ColumnType::UNKNOWN;
    }
  }
  bool isNoteColumn(int k) const { return getColumnType(k) == ColumnType::NOTE; }
  bool isVelocityColumn(int k) const { return getColumnType(k) == ColumnType::VELOCITY; }
  bool isDelayColumn(int k) const { return getColumnType(k) == ColumnType::DELAY; }
  bool isEffectColumn(int k) const { return getColumnType(k) == ColumnType::EFFECT; }
  
  int getNoteNumber(int k) const {
    auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
    return k / n;
  }

  // The [lo, hi] (inclusive) column range sharing k's own note number - its
  // note column, velocity column(s), and delay column, if present. This is
  // the same grouping PatternEditor's getEffectiveSelectionBounds() falls
  // back to with no mark set (one note's worth of columns, not just k
  // alone), and hence what the cursor's own always-on highlight actually
  // covers - see PatternScroll.cpp, which keeps the whole range on screen
  // rather than just k. {k, k} for the effect column, which belongs to no
  // note number (isEffectColumn() callers already special-case it the same
  // way before trusting getNoteNumber()).
  std::pair<int, int> getNoteColumnRange(int k) const {
    if (isEffectColumn(k)) return { k, k };
    auto n = (has_note_column_ ? 1 : 0) + num_velocity_columns_ + (has_delay_column_ ? 1 : 0);
    if (n <= 0) return { k, k };
    auto note = getNoteNumber(k);
    return { note * n, note * n + n - 1 };
  }

  void updateNumSubtracks(int n) {
    if (n > num_subtracks_) num_subtracks_ = n;
  }

  // Only meaningful when color_ordinal_ >= 0 - the caller decides what a
  // negative ordinal (not color-eligible) should look like instead (see
  // PatternEditor::renderHeading()'s grey fallback). Hue is generated
  // from the ordinal via the golden-angle step (360 / phi^2 degrees) -
  // the standard technique for a sequence of hues that stay visually
  // well-spread from each other no matter how many tracks exist, rather
  // than a fixed-size palette that starts repeating after N tracks.
  // Saturation/lightness are fixed - darkened and desaturated enough for
  // white text to sit on top of. All three values are a starting point,
  // tuned by eye - the mechanism (fixed S/L, generated H) is the point,
  // not the exact numbers.
  // The hue alone - factored out of getColor() below so a caller that
  // wants this track's identity at a different saturation/lightness (e.g.
  // a background-content fallback glyph, rendered as foreground text
  // rather than a colored block - see ArrangementGrid.cpp) doesn't have to
  // duplicate the golden-angle formula to get the same hue getColor()
  // would.
  float getHue() const {
    constexpr float kGoldenAngle = 137.50776f;
    return std::fmod(static_cast<float>(color_ordinal_) * kGoldenAngle, 360.0f);
  }

  Color getColor() const {
    constexpr float kSaturation = 0.35f;
    constexpr float kLightness = 0.42f;
    return Color::fromHSL(getHue(), kSaturation, kLightness);
  }

  int num_subtracks_ = 1;
  int num_velocity_columns_ = 0;
  bool has_note_column_ = true;
  bool has_delay_column_ = false;
  bool has_effect_column_ = false;
  // Hides every column's own content in the pattern grid (see
  // getColumnWidth()/PatternEditor::renderRow) while keeping its
  // trailing "│" border, so a track with nothing worth showing yet
  // still visibly occupies its own slot. Mirrors Track::isCollapsed()
  // (SongStructure's baseline copies it in) - a real per-track user
  // toggle, not derived from TrackType.
  bool collapsed_ = false;
  // Only meaningful while collapsed_ - how many blank content cells
  // getColumnWidth() gives the track's sole remaining column, besides
  // its trailing "│" border (see that method's own comment). Defaults
  // to the instrument/percussion/arpeggiator/sample/drum-machine case;
  // SongStructure.cpp sets it to 0 for TrackType::EFFECT, whose
  // collapsed heading toggle lives on its ancestor-row box instead of
  // this level, so its own column needs no cell of its own to hold one.
  int collapsed_content_width_ = 1;
  // Position among color-eligible tracks only (every LeafTrack -
  // see SongStructure::visit(), which assigns this to any track that
  // `dynamic_cast<const LeafTrack *>` succeeds on) in the order
  // they're visited; -1 for anything else (Effect, Group). What
  // getColor() above turns into an actual color; also doubles as "does
  // this track get rendered as a colored track with its own Mute/Solo"
  // (>= 0) - one shared rule for both, rather than two independently-
  // maintained checks that could drift apart.
  int color_ordinal_ = -1;
};

#endif
