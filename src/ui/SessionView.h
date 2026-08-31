#ifndef _SESSIONVIEW_H_
#define _SESSIONVIEW_H_

#include "UIElement.h"

class InputEvent;
class StyleProvider;
class Song;

// A per-track overview: one column per color-eligible leaf track (Song::
// getPlayableTrackIds()), each headed by that track's own ordinal number
// and name, followed by its own Song::getClips(track_id) as a vertical
// list (name and a loop/one-shot glyph only, no length) and then its Send
// Main/A/B levels (read-only for now - the cursor can already land on
// them, in anticipation of editing landing later without a layout
// change) - clips before controls, since a track's clips are what this
// view is primarily for. Every column shares one row axis (header, then
// clip row 0, 1, 2, ..., then the three Send rows) so same-numbered
// clips across different tracks line up on screen, the same "columns =
// tracks" shape ArrangementGrid already uses.
//
// Unlike ArrangementGrid, a column's own background stays plain/uniform
// (styles.window_bg_color) - a track's identity color
// (SongStructure::getBaselineInfo().getColor()) is reserved for its own
// clip cells only, never washed across the whole column.
//
// Takes over PatternEditor's own screen region while open (see UI::layout()/
// renderComponents()) rather than sitting alongside it - opened via the
// "session-view" command (Buffers menu's own "Open Session View" item, no
// keybinding), closed implicitly by any buffer switch (UI's own
// buffer-change listener, matching Emacs's own "switching buffers changes
// what's on screen" precedent) rather than a dedicated close command of its
// own. No editing commands yet - see this class's own offerInput(), which
// only ever moves the cursor.
class SessionView : public UIElement {
 public:
  SessionView(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh, bool focused);
  bool offerInput(const InputEvent & input) override;

  // Lets UI seed the initial column selection from the shared/global track
  // cursor (PatternEditor::getCursorTrackIndex()) when this view opens,
  // the same one-way sync ArrangementGrid's own requestOverviewFocus()
  // uses - not a live two-way binding.
  void setCursorTrackIndex(int track_index) { cursor_track_index_ = track_index; }

 private:
  // Rows within a column, in on-screen order (after the header, which the
  // cursor never lands on): however many clips that column's own track
  // actually has, then the three read-only Send levels - a column with
  // fewer clips than the tallest one just shows blank rows between its
  // own last clip and the Send rows, same "run out early, still share
  // the row axis" idea ArrangementGrid.cpp's per-column loop already
  // relies on for playback resolution (here it's just blank space,
  // nothing to resolve).
  static constexpr int kSendRowCount = 3; // Main, A, B

  int cursor_track_index_ = 0;
  int cursor_row_ = 0; // 0..max_clip_count-1 = clip rows, then the 3 Send rows (render()'s own local max_clip_count)
  int scroll_col_ = 0, scroll_row_ = 0;

  int current_song_version_ = -1;
  int current_cursor_track_index_ = -1, current_cursor_row_ = -1;
  int current_scroll_col_ = -1, current_scroll_row_ = -1;
  bool current_focused_ = false;

  void ensureCursorVisible(const Song & song, int visible_rows, int visible_cols, int num_tracks, int max_rows_needed);
};

#endif
