#ifndef _ARRANGEMENTGRID_H_
#define _ARRANGEMENTGRID_H_

#include "UIElement.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <cellui/DragDrop.h>
#include "TrackDrag.h"
#include <cellui/Fade.h>
#include <cellui/InputDispatcher.h>
#include <cellui/InputEvents.h>
#include <cellui/WheelAcceleration.h>
#include <cellui/Input.h>
#include <cellui/Viewport.h>
#include "StyleProvider.h"
class Song;

// Always-visible overview of the whole song, with time running to the right
// like a composition: one column per bar of the arrangement, running on one
// bar past where its content ends (Song::getArrangementLength()). Rows =
// tracks - Song::getPlayableTrackIds(), each named at the left after a
// swatch of its identity color (the same one PatternEditor's own heading row
// uses). Above them two header rows: a bar ruler (the number of every
// fourth bar) and the song's locators (Song::getLocators()), each a "›" at
// its bar followed by its name. A clip instance renders as a colored block
// (that track's own identity color) spanning its own active length in bars,
// its leading bar showing a single hex digit - its ordinal position in that
// track's own clip list, the exact index Launchpad Live View's rows already
// address - resolved the same way real playback does (ArrangementOps.h's own
// resolveInstanceForBar(), once per bar); every later bar it's still active
// through repeats the same color with a blank cell instead of the digit. A
// colored background is reserved for an active instance - a bar with no
// active instance instead falls back to a plain grey foreground glyph
// (uncolored on purpose - a track's own identity color never means anything
// but "a real instance is here"; ASCII rather than a wide Unicode glyph,
// since a cell only ever has room for one column) showing whether the
// background has anything there at all - "*" for a real sounding note, "."
// for a bar that's only non-empty from note-offs/commands.
//
// No per-cell copy/paste - placing clip content is copy-to-clip's own job,
// from PatternEditor (a placement is moved by dragging it, in the window);
// this widget is placement/overview only,
// and an instantiated clip can only be edited by going to one of its own
// (live-linked) instances in PatternEditor, never here directly. No
// track-ordinal header either.
//
// This class's own cursor is local and passive: moving it never touches
// PatternEditor, the playhead, or any track selection - the one
// exception is the viewport's own scroll position, which follows the
// playhead instead of the cursor while playing (regardless of focus) or
// while stopped with focus elsewhere, so the playhead never scrolls
// itself out of view (see render()'s own comment on why exactly one of
// the two, never both, drives the scroll position in any given frame).
// Enter commits the (track, row) under the cursor to shared state - see
// commit_callback_ below, which UI wires up (this class has no idea
// PatternEditor, the playhead, or Launchpad even exist).
class ArrangementGrid : public UIElement {
 public:
  // The view jumps a page of bars when the cursor or the playhead leaves it, instead of a bar at a time.
  void setPageScroll(bool page) { page_scroll_ = page; }
  // The focus level is still easing: the loop keeps drawing frames.
  bool isAnimating() const { return fades_.active() || focus_fade_drawn_; }

  ArrangementGrid(Plane & parent);

  // The header's rows (bar ruler, locators) and the names' width, in cells.
  static constexpr int kHeaderRows = 2;
  static constexpr int kNameWidth = 10;

  // The bars are a plane of their own (so they can shift by parts of a bar
  // under the names); this shows or hides it with the grid.
  void setShown(bool shown) {
    shown_ = shown;
    setVisible(shown);
    if (bars_plane_) bars_plane_->setVisible(shown);
  }

  // `focused` (whether this widget is UI::active_element_ - it has no way
  // to know that itself) gates the cursor-cell highlight only: distracting
  // otherwise, since it'd stay lit even while input is going somewhere
  // else entirely (PatternEditor, most of the time). `selected_track_id`
  // is the *shared* track selection (PatternEditor::getCursorTrackIndex(),
  // resolved to a real id by UI - the same one Launchpad Live View
  // already follows) - its own column brightens whichever clip instances
  // sit in it (never the plain background, which has no "selected" state
  // of its own to show) regardless of focus, since it's not this widget's
  // own local state to gate on that the way the cursor cell's is.
  bool render(const StyleProvider & styles, bool refresh, bool focused, int selected_track_id);
  // A key that went down; the widget's own handler for KeyDown.
  bool keyDown(const cellui::KeyDown & key);

  // Called (once, from UI::initialize()) with the (track_id, row) under
  // the cursor whenever Enter commits it - row is the first row of the
  // cursor's bar, where PatternEditor should land. Not a
  // commands_-registered command - Enter plays the same special,
  // directly-checked role here that it already does in PatternEditor's
  // own keyDown().
  void setCommitCallback(std::function<void(int track_id, int row)> cb) { commit_callback_ = std::move(cb); }

  // Song::getRootTrackIds() filtered down to color-eligible tracks only
  // (VisibleTrackInfo::color_ordinal_ >= 0 - every LeafTrack: Instrument/
  // Sample/Percussion/DrumMachine, never an Effect). Public (not just
  // render()'s own internal use) so a Launchpad in GridMode::LIVE can
  // show exactly the same columns this widget does, rather than a
  // separately-derived list that could disagree with it.
  std::vector<int> getVisibleTrackIds(const Song & song) const;

  // The first row of the cursor's bar - where a Launchpad Live View
  // "assign" press made while stopped places the picked clip.
  int getCursorRow(const Song & song) const;

  // Moves the cursor by `delta` bars - Launchpad's own Live View wires
  // its up/down buttons to this (via UI), since Live View has no row
  // scroll of its own for them to drive.
  void moveCursorBar(int delta);

  // Whether the view is still moving toward where it was scrolled to (on a
  // display that scrolls smoothly), so another frame is due.
  bool animating() const { return viewport_.animating(); }

 private:
  bool page_scroll_ = false;
  bool pick(int screen_row, int screen_col);

  // --- Dragging a placed clip (window only, cellui/DragDrop.h). A press on a
  // block is a candidate; moving on with the button held lifts the block: its
  // bars show empty, a ghost follows the pointer, and the bars it would
  // cover show it at half strength. A drop moves the placement to the bar
  // under the grabbed point (Ctrl keeps the old one). Onto another track of
  // the same kind the clip goes as an independent copy in that track's first
  // free slot. The clip itself, and so its other placements, stay.
  struct InstanceDrag {
    int track_id = 0, start_row = 0, clip_index = 0, track_type = -1;
    int first_bar = 0;   // where the block starts
    int bars = 1;        // how many bars it spans as placed (a looping clip runs on to the next event or the end)
    int pass_bars = 1;   // how many one pass of the clip takes: what is dragged and shown
    int grab_offset = 0; // the bar grabbed, counted from the block's first, within one pass
  };
  struct DragCandidate {
    int track_index = 0, bar = 0; // bar -1: the track's name
    float y = 0, x = 0;
  };
  // A track picked up by its name: its row and the one it would take the place of.
  std::optional<int> trackRowAt(int screen_row) const;
  std::string trackLabel(int track_id) const;
  void startTrackDrag(const DragCandidate & candidate, const cellui::MouseMove & e);
  bool trackDragOver(const cellui::DragOver & e);
  bool trackDrop(const cellui::Drop & e);
  std::optional<int> track_drag_source_, track_drop_target_;
  // A run of bars on a track, by its index in getVisibleTrackIds().
  struct BarSpan {
    int track_index = 0, first_bar = 0, bars = 0;
    bool contains(int track, int bar) const { return track == track_index && bar >= first_bar && bar < first_bar + bars; }
  };
  // The (track index, bar) at a cell of the bars.
  std::optional<std::pair<int, int>> cellAt(int screen_row, int screen_col) const;
  // The block with a clip at (track_index, bar), as a drag payload.
  std::optional<InstanceDrag> blockAt(int track_index, int bar) const;
  void startInstanceDrag(const cellui::MouseMove & e);
  bool dragOver(const cellui::DragOver & e);
  bool drop(const cellui::Drop & e);
  std::optional<DragCandidate> drag_candidate_;
  std::optional<BarSpan> drag_source_, drop_preview_;
  int drag_clip_index_ = 0;
  bool drag_copy_ = false;
  std::unique_ptr<cellui::DragGhost> ghost_;
  unsigned drag_version_ = 0, current_drag_version_ = 0;
  std::vector<cellui::Connection> connections_;
  // How many bar rows there are to show: the arrangement's own bars plus
  // one empty one past its end to place into, reaching the cursor and the
  // playhead wherever they are.
  int barCount(const Song & song, int playing_bar) const;

  // Always clamps the cursor to whatever bars/tracks actually exist, and
  // the scroll position to whatever range is currently valid. Only when
  // `follow_cursor` is set (this widget is focused - see render()'s own
  // comment on why not otherwise) does it also slide
  // the viewport just far enough that the (possibly newly
  // clamped) cursor is back inside the visible viewport - never scrolls
  // further than that, so the viewport only moves when the cursor's
  // movement actually pushed it out of view.
  void ensureCursorVisible(int bar_count, int visible_rows, int visible_cols, int num_tracks, bool follow_cursor);

  // Cursor position: a bar of the arrangement, and an index into
  // getVisibleTrackIds() (not a raw track_id, so moving the cursor is just
  // a bounds-clamped increment/decrement).
  int cursor_bar_ = 0;
  int cursor_track_index_ = 0;

  // Top-left corner of the visible viewport, in tracks (rows) and bars (cols) - kept in
  // sync with the cursor by ensureCursorVisible() rather than tracked
  // independently, so the cursor is always on screen.
  cellui::Viewport viewport_;
  static constexpr int kWheelBars = 2; // bars one wheel notch scrolls, before acceleration
  cellui::WheelAcceleration wheel_acceleration_;

  // What render() last drew, so it can skip redrawing when nothing this
  // widget actually shows has changed - same dirty-check shape InfoLine's
  // own render() already uses.
  int current_song_version_ = -1;
  int current_playing_row_ = -1;
  int current_cursor_bar_ = -1, current_cursor_track_index_ = -1;
  double current_shown_col_ = -1;
  int current_phase_ = -1;
  bool shown_ = true, current_shown_ = true;
  std::unique_ptr<Plane> bars_plane_;
  std::tuple<int, int, int, int> laid_out_{-1, -1, -1, -1};
  bool current_smooth_playhead_ = false;
  int current_scroll_row_ = -1;
  std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
  bool current_focused_ = false;
  float focus_ = 0.0f;
  cellui::Fades fades_;
  bool focus_fade_drawn_ = false;
  int current_selected_track_id_ = -1;

  std::function<void(int track_id, int row)> commit_callback_;
};

#endif
