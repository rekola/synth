#ifndef _PATTERNMATRIX_H_
#define _PATTERNMATRIX_H_

#include "UIElement.h"
#include "../model/Pattern.h"

#include <functional>
#include <optional>
#include <vector>

class InputEvent;
class StyleProvider;
class Song;

// Always-visible read-only overview of the whole song: rows = scenes
// (Song::getScenes()'s existing sequential order), columns = tracks -
// Song::getRootTrackIds() filtered down to only color-eligible ones (see
// getVisibleTrackIds()) - one character cell per (scene, track) showing
// whether that track has content in that scene. Plus single-cell copy/paste
// (kill-ring-save/yank,
// reusing PatternEditor's own command names/chords rather than inventing
// "copy"/"paste" - see UI::executeCommand()'s "active element's own
// registry first" dispatch, which is what lets the same M-w/C-y keystroke
// mean something different here than in PatternEditor without either
// class knowing about the other).
//
// This class's own cursor is local and passive: moving it never touches
// PatternEditor, the playhead, or any track selection. Only Enter commits
// the cell under the cursor to shared state - see commit_callback_ below,
// which UI wires up (this class has no idea PatternEditor, the playhead,
// or Launchpad even exist, the same "PatternEditor has no idea these
// happen to feed a Launchpad device" separation PatternEditor.h's own
// getCursorTrackIndex()/setCursorTrack() already establish).
class PatternMatrix : public UIElement {
 public:
  PatternMatrix(UIPlane & parent);

  // `focused` (whether this widget is UI::active_element_ - it has no way
  // to know that itself) gates the cursor-cell highlight only: distracting
  // otherwise, since it'd stay lit even while input is going somewhere
  // else entirely (PatternEditor, most of the time).
  bool render(const StyleProvider & styles, bool refresh, bool focused);
  bool offerInput(const InputEvent & input) override;

  // Called (once, from UI::initialize()) with the (track_id, scene index)
  // under the cursor whenever Enter commits it. Not a commands_-registered
  // command - Enter plays the same kind of special, directly-checked role
  // here that it already does in PatternEditor's own offerInput() (reader
  // commit, annotation edit, ...), not a keymap-bound one.
  void setCommitCallback(std::function<void(int track_id, int scene_idx)> cb) { commit_callback_ = std::move(cb); }

  // Called when Right is pressed with the cursor already on the last
  // (rightmost) track column - mirrors PatternEditor::
  // setOverviewRequestCallback() exactly, the opposite edge: leaving the
  // overview back into PatternEditor rather than entering it (lands on its
  // first track - see UI::exitOverview() - not something this class needs
  // to know).
  void setExitRightCallback(std::function<void()> cb) { exit_right_callback_ = std::move(cb); }

  // Song::getRootTrackIds() filtered down to color-eligible tracks only
  // (VisibleTrackInfo::color_ordinal_ >= 0 - every LeafTrack: Instrument/
  // Sample/Percussion/DrumMachine, never an Effect). An Effect track can
  // still carry its own per-scene Command automation (a distinct track_id
  // in the same Scene, keyed independently of whatever leaf track it sits
  // under), but it never gets a column here - both because it's not what
  // this overview is meant to show at a glance, and because kill-ring-save/
  // yank operate on exactly one visible track_id's own Pattern and
  // deliberately never reach into a track's nested Effect children at all -
  // more than one leaf track can share the same parent Effect subtree, so
  // there's no single owning cell a shared Effect's Command data could
  // safely be folded into (see the "kill-ring-save" command's own comment).
  // Public (not just render()'s own internal use) so a Launchpad in
  // GridMode::OVERVIEW can show exactly the same columns this widget does,
  // rather than a separately-derived list that could disagree with it.
  std::vector<int> getVisibleTrackIds(const Song & song) const;

  // Lets UI place the column cursor explicitly when handing focus here -
  // e.g. requestOverviewFocus() lands on the last (rightmost) column when
  // entering from PatternEditor's own leftmost track, so the two views
  // read as one continuous horizontal strip rather than always resetting
  // to wherever the cursor happened to be left last time. Mirrors
  // PatternEditor::setCursorTrack() exactly, same reasoning: out-of-range
  // values are harmless, ensureCursorVisible() clamps on the next render().
  void setCursorTrackIndex(int track_index) { cursor_track_index_ = track_index; }

 private:
  // Cursor position, in the same terms as the axes above: an absolute
  // scene index (not scroll-relative) and an index into getRootTrackIds()
  // (not a raw track_id, so moving the cursor is just a bounds-clamped
  // increment/decrement).
  int cursor_scene_ = 0;
  int cursor_track_index_ = 0;

  // Top-left corner of the visible viewport, in the same terms as the
  // cursor above - kept in sync with it by ensureCursorVisible() rather
  // than tracked independently, so the cursor is always on screen.
  int scroll_row_ = 0, scroll_col_ = 0;

  // The single copied cell - a whole Pattern (every row) for one track,
  // deliberately not PatternEditor's own ClipboardEntry/PatternBlock (see
  // this class's own header comment above) - and the track_id it came
  // from, since yank always targets that same track regardless of where
  // the cursor has moved to since the copy (see the "kill-ring-save"
  // command below).
  std::optional<Pattern> cell_clipboard_;
  int cell_clipboard_track_id_ = -1;

  // What render() last drew, so it can skip redrawing when nothing this
  // widget actually shows has changed - same dirty-check shape InfoLine's
  // own render() already uses.
  int current_song_version_ = -1;
  int current_playing_scene_ = -1;
  int current_cursor_scene_ = -1, current_cursor_track_index_ = -1;
  int current_scroll_row_ = -1, current_scroll_col_ = -1;
  bool current_focused_ = false;

  // Clamps the cursor to whatever scenes/tracks actually exist, then slides
  // scroll_row_/scroll_col_ just far enough that the (possibly newly
  // clamped) cursor is back inside the visible viewport - never scrolls
  // further than that, so the viewport only moves when the cursor's
  // movement actually pushed it out of view.
  void ensureCursorVisible(int visible_rows, int visible_cols, int num_scenes, int num_tracks);

  std::function<void(int track_id, int scene_idx)> commit_callback_;
  std::function<void()> exit_right_callback_;
};

#endif
