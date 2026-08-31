#include "SessionView.h"

#include "../playback/InputEvent.h"
#include "StyleProvider.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/LeafTrack.h"
#include "../model/Clip.h"
#include "../model/SongStructure.h"
#include "../util/Utf8.h"

#include <algorithm>
#include <fmt/core.h>

using namespace std;

namespace {

const LeafTrack *
asLeafTrack(const Song & song, int track_id) {
  return dynamic_cast<const LeafTrack *>(song.getMasterTrack().getChildByInternalId(track_id));
}

}

SessionView::SessionView(UIPlane & parent) : UIElement(parent) {
  assertCommandBindingsValid();
}

void
SessionView::ensureCursorVisible(const Song & song, int visible_rows, int visible_cols, int num_tracks, int max_rows_needed) {
  (void) song;
  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));
  cursor_row_ = clamp(cursor_row_, 0, max(0, max_rows_needed - 1));

  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));
  scroll_row_ = clamp(scroll_row_, 0, max(0, max_rows_needed - visible_rows));

  if (cursor_track_index_ < scroll_col_) scroll_col_ = cursor_track_index_;
  if (visible_cols > 0 && cursor_track_index_ >= scroll_col_ + visible_cols) scroll_col_ = cursor_track_index_ - visible_cols + 1;
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));

  if (cursor_row_ < scroll_row_) scroll_row_ = cursor_row_;
  if (visible_rows > 0 && cursor_row_ >= scroll_row_ + visible_rows) scroll_row_ = cursor_row_ - visible_rows + 1;
  scroll_row_ = clamp(scroll_row_, 0, max(0, max_rows_needed - visible_rows));
}

bool
SessionView::offerInput(const InputEvent & input) {
  if (dispatchCommand(input)) return true;
  if (input.getKind() == InputEvent::Kind::RELEASE) return false;

  // const - Song::getClips() has a non-const overload that inserts an
  // empty entry for a track that doesn't have one yet (needed for actual
  // mutation elsewhere, e.g. ArrangementOps.cpp); this class only ever
  // reads, so binding const here (like ArrangementOps.h's own
  // resolveReadTarget()) keeps every getClips() call below side-effect
  // free regardless of which overload it'd otherwise resolve to.
  const Song & song = getController().getSong();
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  // Read-only navigation only in this pass - no Enter/rename/delete/loop-
  // toggle, no Send-level editing (see this class's own header comment).
  if (input.getId() == NCKEY_UP) cursor_row_--;
  else if (input.getId() == NCKEY_DOWN) cursor_row_++;
  else if (input.getId() == NCKEY_LEFT) cursor_track_index_--;
  else if (input.getId() == NCKEY_RIGHT) cursor_track_index_++;
  else if (input.getId() == NCKEY_PGUP) cursor_row_ -= getDim().first;
  else if (input.getId() == NCKEY_PGDOWN) cursor_row_ += getDim().first;
  else return false;

  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));
  cursor_row_ = max(0, cursor_row_);
  return true;
}

bool
SessionView::render(const StyleProvider & styles, bool refresh, bool focused) {
  const Song & song = getController().getSong(); // see offerInput()'s own comment on why const
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  auto [ rows, cols ] = getDim();
  if (rows < 2 || cols < 1) return false;

  constexpr int kColWidth = 14; // content width; one divider column follows each
  auto visible_rows = max(0, rows - 1); // row 0 is the header, never scrolled
  auto visible_cols = max(0, cols) / (kColWidth + 1);

  // Clips come first (rows [0, max_clip_count)), the three Send rows
  // after (rows [max_clip_count, max_clip_count + kSendRowCount)) - a
  // track with fewer clips than the tallest one just leaves blank rows
  // between its own last clip and the Send rows, the same "run out early,
  // still share the row axis" idea every column already tolerates.
  // max_clip_count is the tallest column's own clip count, maxed across
  // every track so the shared row axis reaches every column's own last
  // clip.
  int max_clip_count = 0;
  for (auto track_id : track_ids) {
    max_clip_count = max(max_clip_count, static_cast<int>(song.getClips(track_id).size()));
  }
  int max_rows_needed = max_clip_count + kSendRowCount;

  ensureCursorVisible(song, visible_rows, visible_cols, num_tracks, max_rows_needed);

  auto new_version = song.getMajorVersion();
  if (!refresh && new_version == current_song_version_ &&
      cursor_track_index_ == current_cursor_track_index_ && cursor_row_ == current_cursor_row_ &&
      scroll_col_ == current_scroll_col_ && scroll_row_ == current_scroll_row_ &&
      focused == current_focused_) {
    return false;
  }
  current_song_version_ = new_version;
  current_cursor_track_index_ = cursor_track_index_;
  current_cursor_row_ = cursor_row_;
  current_scroll_col_ = scroll_col_;
  current_scroll_row_ = scroll_row_;
  current_focused_ = focused;

  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  erase();
  // erase() alone leaves this plane's own base cell showing through
  // wherever nothing below explicitly writes a cell (its own semi-
  // transparent debug base, UIPlane::createChild()) - this widget shares
  // its exact screen rect with pattern_editor_ (see UI::layout()), so
  // any such gap would show pattern_editor_'s last real content bleeding
  // through underneath rather than a plain blank. fill() (plain spaces,
  // the color just set) guarantees every cell in the plane is genuinely
  // opaque before anything more specific gets drawn over it - the same
  // reasoning ArrangementGrid's own per-column loop satisfies by simply
  // never leaving a column unwritten in the first place (it always draws
  // a blank/background glyph for an out-of-range column rather than
  // stopping early - see its own render()).
  fill();

  SongStructure structure(song);

  for (auto vc = 0; vc < visible_cols; vc++) {
    auto track_index = scroll_col_ + vc;
    if (track_index >= num_tracks) break;
    auto track_id = track_ids[static_cast<size_t>(track_index)];
    auto x = vc * (kColWidth + 1);
    auto * leaf = asLeafTrack(song, track_id);

    // Header (row 0, never part of the scrolled/cursor-addressable row
    // axis below) - the exact same "T<N> name" label PatternEditor's own
    // renderHeading() draws (N = color_ordinal_, not track_index - the
    // same stable-across-scroll count that also picks the track's own
    // identity color), upright T<N> prefix followed by the track's own
    // name in italic. Plain accent color, like every other non-clip cell
    // in this column - the track's own identity color is reserved for
    // its clip cells alone.
    setFgColor(styles.window_accent_fg_color);
    setBgColor(styles.window_bg_color);
    auto prefix = "T" + std::to_string(structure.getBaselineInfo(track_id).color_ordinal_);
    auto friendly_name = (leaf && !leaf->getName().empty()) ? " " + leaf->getName() : string();
    auto header = Utf8::padToWidth(Utf8::truncateToWidth(prefix + friendly_name, kColWidth), kColWidth);
    auto upright_len = std::min(header.size(), prefix.size());
    putstr(0, x, header.substr(0, upright_len));
    if (upright_len < header.size()) {
      setItalic(true);
      putstr(0, x + static_cast<int>(upright_len), header.substr(upright_len));
      setItalic(false);
    }

    for (auto vr = 0; vr < visible_rows; vr++) {
      auto row = scroll_row_ + vr;
      auto y = 1 + vr;
      bool is_cursor_cell = focused && track_index == cursor_track_index_ && row == cursor_row_;

      string text;
      bool is_clip_row = row < max_clip_count;
      if (is_clip_row) {
        auto clip_row = static_cast<size_t>(row);
        auto & clips = song.getClips(track_id);
        if (clip_row < clips.size()) {
          auto & clip = clips[clip_row];
          auto name = clip.getName().empty() ? "(unnamed)" : clip.getName();
          text = fmt::format("{} {}", clip.isLooping() ? "↻" : "▶", name);
        }
      } else if (leaf && row - max_clip_count < kSendRowCount) {
        // The bound above matters: without it, every row past the three
        // real Send rows (down to visible_rows, however far past
        // max_rows_needed the screen actually is) would still fall
        // through the label ternary below - send_row == 0 is Main,
        // send_row == 1 is A, and *everything else* (2, 3, 4, ...)
        // landed on B, so the whole rest of the column filled with
        // repeated "B" rows instead of staying blank.
        auto send_row = row - max_clip_count;
        auto & sends = leaf->getSends();
        float level = send_row == 0 ? sends.main : (send_row == 1 ? sends.a : sends.b);
        const char * label = send_row == 0 ? "Main" : (send_row == 1 ? "A" : "B");
        text = fmt::format("{:<4}{:4.0f}%", label, level * 100.0f);
      }
      text = Utf8::padToWidth(Utf8::truncateToWidth(text, kColWidth), kColWidth);

      Color fg = styles.window_fg_color, bg = styles.window_bg_color;
      // A clip row's own identity color - the one place color appears
      // below the header, reserved for real clip content only (a blank
      // row, or a Send row, stays plain).
      if (is_clip_row && static_cast<size_t>(row) < song.getClips(track_id).size()) {
        bg = structure.getBaselineInfo(track_id).getColor();
        fg = Color(255, 255, 255);
      }
      if (is_cursor_cell) {
        fg = styles.highlight_fg_color;
        bg = styles.highlight_bg_color;
      }
      setFgColor(fg);
      setBgColor(bg);
      putstr(y, x, text);
    }

    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    for (auto y = 0; y < rows; y++) putstr(y, x + kColWidth, "│");
  }

  return true;
}
