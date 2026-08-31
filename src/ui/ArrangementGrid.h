#ifndef _ARRANGEMENTGRID_H_
#define _ARRANGEMENTGRID_H_

#include "UIElement.h"

#include <functional>
#include <vector>

class InputEvent;
class StyleProvider;
class Song;

// Always-visible overview of the whole song: each scene occupies a title
// row (its own name, spanning the full width - no per-scene numbering
// any more, a name is how scenes are told apart) followed by its own
// bar rows (Song::getRowsPerBar() rows each - a scene spans as many bar
// rows as it has bars; today every scene shares Song::getPatternLength()/
// getRowsPerBar() bars, uniformly - see barsPerScene()'s own comment).
// Columns (within the bar rows) = tracks - Song::getRootTrackIds()
// filtered down to only color-eligible ones (see getVisibleTrackIds()). A
// clip instance renders as a colored capsule (that track's own identity
// color, the same one PatternEditor's own heading row uses) two cells
// wide - a full identifier cell showing a single hex digit (its ordinal
// position in that track's own clip list, the exact index Launchpad
// Session view's rows already address) flanked by half-width padding
// cells shared with whichever neighboring track/edge sits on the other
// side (see render()'s own half-block drawing) - confined to the one
// scene it's placed in, resolved the same way real playback does
// (ArrangementOps.h's own resolveInstanceAt(), once per bar's own leading
// row); every later bar it's still active through repeats the same
// capsule with a blank identifier cell instead of the digit. A colored
// background is reserved for an active instance - a bar with no active
// instance instead falls back to a plain grey foreground glyph (the same
// grey every other untouched cell's text already uses, uncolored on
// purpose - a track's own identity color never means anything but "a
// real instance is here"; ASCII rather than a wide Unicode glyph, since
// the identifier cell only ever has room for one column) showing whether
// the background has anything there at all - "*" for a real sounding
// note, "." for a bar that's only non-empty from
// note-offs/commands, bar-scoped.
//
// No per-cell copy/paste - placing/moving clip content is copy-to-clip's
// own job, from PatternEditor; this widget is placement/overview only,
// and an instantiated clip can only be edited by going to one of its own
// (live-linked) instances in PatternEditor, never here directly. No
// track-ordinal header row either.
//
// This class's own cursor is local and passive: moving it never touches
// PatternEditor, the playhead, or any track selection - the one
// exception is the viewport's own scroll position, which follows the
// playhead instead of the cursor while playing (regardless of focus) or
// while stopped with focus elsewhere, so the playhead never scrolls
// itself out of view (see render()'s own comment on why exactly one of
// the two, never both, drives the scroll position in any given frame).
// On a bar row, Enter commits the (track, scene, row) under the cursor to shared state
// - see commit_callback_ below, which UI wires up (this class has no
// idea PatternEditor, the playhead, or Launchpad even exist). On a title
// row, Enter instead opens that scene's own name for editing in place -
// see startSceneRename().
class ArrangementGrid : public UIElement {
 public:
  ArrangementGrid(UIPlane & parent);

  // `focused` (whether this widget is UI::active_element_ - it has no way
  // to know that itself) gates the cursor-cell highlight only: distracting
  // otherwise, since it'd stay lit even while input is going somewhere
  // else entirely (PatternEditor, most of the time).
  bool render(const StyleProvider & styles, bool refresh, bool focused);
  bool offerInput(const InputEvent & input) override;

  // Called (once, from UI::initialize()) with the (track_id, scene_idx,
  // row) under the cursor whenever Enter commits it - row is the exact
  // row PatternEditor should land on (cursor_bar_ * rows_per_bar), since
  // the cursor has bar-level granularity within a scene, not just a
  // scene-level one. Not a commands_-registered command - Enter plays
  // the same special, directly-checked role here that it already does in
  // PatternEditor's own offerInput() (reader commit, annotation edit,
  // ...), not a keymap-bound one.
  void setCommitCallback(std::function<void(int track_id, int scene_idx, int row)> cb) { commit_callback_ = std::move(cb); }

  // Called when Right is pressed with the cursor already on the last
  // (rightmost) track column - leaving the overview back into
  // PatternEditor rather than a no-op clamp (lands on its first track -
  // see UI::exitOverview() - not something this class needs to know).
  void setExitRightCallback(std::function<void()> cb) { exit_right_callback_ = std::move(cb); }

  // Song::getRootTrackIds() filtered down to color-eligible tracks only
  // (VisibleTrackInfo::color_ordinal_ >= 0 - every LeafTrack: Instrument/
  // Sample/Percussion/DrumMachine, never an Effect). Public (not just
  // render()'s own internal use) so a Launchpad in GridMode::SESSION can
  // show exactly the same columns this widget does, rather than a
  // separately-derived list that could disagree with it.
  std::vector<int> getVisibleTrackIds(const Song & song) const;

  // Lets UI place the column cursor explicitly when handing focus here -
  // e.g. requestOverviewFocus() lands on the last (rightmost) column when
  // entering from PatternEditor's own leftmost track, so the two views
  // read as one continuous horizontal strip rather than always resetting
  // to wherever the cursor happened to be left last time.
  void setCursorTrackIndex(int track_index) { cursor_track_index_ = track_index; }

  // The scene index the cursor currently sits on - Launchpad's own
  // Session view reads this (via UI::renderComponents()'s own
  // SessionWindow) so a pad's "assign" press knows which scene to write
  // the picked clip's pattern into, matching this grid's own displayed
  // cursor rather than a separately-tracked position.
  int getCursorScene() const { return cursor_scene_; }

  // Moves the cursor by one scene (+1/-1), landing on that scene's own
  // bar 0 - the same clamp offerInput()'s own NCKEY_UP/NCKEY_DOWN
  // handling uses (one position past the last real Scene is still
  // valid), just at this method's own coarser whole-scene granularity.
  // Launchpad's own Session view wires its up/down buttons to this (via
  // UI) rather than scrolling its own pad-grid row window, since Session
  // view has no per-bar concept of its own to move by.
  void moveCursorScene(const Song & song, int delta);

 private:
  // Bars per scene, uniform across every scene for now (every scene
  // shares Song::getPatternLength()/getRowsPerBar()) - the one place
  // that assumption lives, so it's the one place to change once a scene
  // can have its own length. Always at least 1, even with a degenerate
  // rowsPerBar/patternRows configuration - a grid needs somewhere to put
  // the cursor regardless.
  int barsPerScene(const Song & song) const;

  // Cursor position: an absolute scene index (not scroll-relative - one
  // position past the last real Scene is a valid, virtual target, same
  // as the row axis below), a bar within that scene (0-indexed, or -1 for
  // that scene's own title row), and an index into getRootTrackIds() (not
  // a raw track_id, so moving the cursor is just a bounds-clamped
  // increment/decrement) - meaningless while cursor_bar_ is -1, since the
  // title row has no per-track columns of its own.
  int cursor_scene_ = 0;
  int cursor_bar_ = -1;
  int cursor_track_index_ = 0;

  // Top-left corner of the visible viewport - scroll_row_ in the same
  // flattened (scene * barsPerScene() + bar) units moveCursorRow() uses,
  // kept in sync with the cursor by ensureCursorVisible() rather than
  // tracked independently, so the cursor is always on screen.
  int scroll_row_ = 0, scroll_col_ = 0;

  // What render() last drew, so it can skip redrawing when nothing this
  // widget actually shows has changed - same dirty-check shape InfoLine's
  // own render() already uses.
  int current_song_version_ = -1;
  int current_playing_scene_ = -1, current_playing_row_ = -1;
  int current_cursor_scene_ = -1, current_cursor_bar_ = -1, current_cursor_track_index_ = -1;
  int current_scroll_row_ = -1, current_scroll_col_ = -1;
  bool current_focused_ = false;
  // Set directly (not derivable from the dirty-check fields above) right
  // after opening or closing the rename reader - neither touches
  // Song::getMajorVersion() by itself (a cancel never touches the model
  // at all, and a commit's own song.incVersion() only covers the success
  // case), so without this the blanked/reader-covered title row could sit
  // unrepainted until some unrelated redraw happens to fire.
  bool force_redraw_ = false;

  // Moves the cursor by `delta` rows, flattened across the whole (real +
  // one virtual) scene range, each scene occupying 1 (its own title row)
  // + barsPerScene() rows - crossing a scene boundary lands on the
  // adjacent scene's own title row or last bar, so Up/Down read as one
  // continuous timeline rather than being fenced in by whichever scene
  // the cursor started in.
  void moveCursorRow(const Song & song, int delta);

  // Always clamps the cursor to whatever scenes/bars/tracks actually
  // exist, and the scroll position to whatever range is currently valid.
  // Only when `follow_cursor` is set (this widget is focused - see
  // render()'s own comment on why not otherwise) does it also slide
  // scroll_row_/scroll_col_ just far enough that the (possibly newly
  // clamped) cursor is back inside the visible viewport - never scrolls
  // further than that, so the viewport only moves when the cursor's
  // movement actually pushed it out of view.
  void ensureCursorVisible(const Song & song, int visible_rows, int visible_cols, int num_tracks, bool follow_cursor);

  // Opens the cursor's own scene name for in-place editing (Enter, while
  // cursor_bar_ is -1) - positions the reader at exactly the screen row
  // ensureCursorVisible() already guarantees the title row occupies
  // (cursor_scene_'s own flat position minus scroll_row_), so unlike
  // PatternEditor's own analogous editors (annotation/track-name) this
  // needs no separately cached screen coordinate from the last render()
  // pass. Committing/canceling is handled inline in offerInput(), mirroring
  // PatternEditor::offerInput()'s own reader-active handling.
  void startSceneRename();

  int renaming_scene_idx_ = -1;

  std::function<void(int track_id, int scene_idx, int row)> commit_callback_;
  std::function<void()> exit_right_callback_;
};

#endif
