#ifndef _SESSIONVIEW_H_
#define _SESSIONVIEW_H_

#include "../UIElement.h"

#include <functional>
#include <string>

class InputEvent;
class StyleProvider;
class Song;

// A per-track overview: one column per color-eligible leaf track (Song::
// getPlayableTrackIds()), each headed by that track's own ordinal number
// and name (F2 renames it - see startTrackRename()) plus its Mute/Solo
// state (an "M"/"S" pair at the header row's own right edge - dim when
// off, colored when on; there's no dedicated Mute/Solo row any more, see
// kLogicalToPhysical's own comment), on its own dark grey backdrop
// (styles.window_accent_bg_color) setting the header apart from the
// plain window background below it, followed by its own
// Song::getClips(track_id) as a vertical list of exactly
// kClipRowCount slots (a leading play glyph plus name for a real clip,
// with a further repeat glyph for one that loops, or a leading stop glyph
// for a slot with none - always all of them, not just however many clips
// happen to exist, so every track's own row axis is identical and a
// track can never "run out" mid-column the way a data-sized row count
// would), then two further sections - Send levels
// (Main/A/B, in dB) and ambisonic direction (azimuth/elevation/distance)
// - each its own fixed slice of the shared row axis, separated by a plain
// divider row. Every column shares this one row axis so same-numbered
// rows line up between tracks, the same "columns = tracks" shape
// ArrangementGrid already uses.
//
// Unlike ArrangementGrid, a column's own background stays plain/uniform
// (styles.window_bg_color) - a track's identity color
// (SongStructure::getBaselineInfo().getColor()) is reserved for its own
// populated clip cells only, never washed across the whole column.
//
// Takes over PatternEditor's own screen region while open (see UI::layout()/
// renderComponents()) rather than sitting alongside it - opened via the
// "session-view" command (Buffers menu's own "Open Session View" item, no
// keybinding), closed implicitly by any buffer switch (UI's own
// buffer-change listener, matching Emacs's own "switching buffers changes
// what's on screen" precedent) rather than a dedicated close command of its
// own.
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

  // Read-only counterparts, for Controller::setSessionViewCursor() (kept
  // current by UI::renderComponents() every frame) - the column index
  // among Song::getPlayableTrackIds() the cursor is currently on, and its
  // own clip-list index (Song::getClips(track_id)) when the cursor is on
  // a real clip row at all (-1 otherwise - the header, or the Sends/
  // Direction rows, name no clip slot). Mirrors delete-clip's own
  // identical "a CLIP row's own physical offset doubles as its clip-list
  // index" resolution (see rowKindFor()/kLogicalToPhysical's own comment).
  int getCursorTrackIndex() const { return cursor_track_index_; }
  int getCursorClipIndex() const { return rowKindFor(cursor_row_) == RowKind::CLIP ? kLogicalToPhysical[cursor_row_] : -1; }

  // Called on Enter over a clip row (populated or not) with the row's own
  // (track_id, clip_index) - Enter acts exactly like a Launchpad Session
  // view pad press landing on that same cell (LaunchpadManager::
  // triggerSessionClip()), never a separate "focus for editing" gesture
  // of its own, the same callback-not-reaching-into-UI pattern
  // ArrangementGrid's own commit_callback_ already uses (this class has
  // no idea LaunchpadManager exists either). Wired in UI::start().
  void setTriggerCallback(std::function<void(int track_id, int clip_index)> cb) { trigger_callback_ = std::move(cb); }

 private:
  // Always exactly this many clip rows per column, whether or not that
  // many clips actually exist on the track - see this class's own header
  // comment on why "run out early" isn't a thing here any more (unlike
  // the old data-sized row count, or ArrangementGrid.cpp's own per-column
  // loop tolerating it for playback resolution).
  static constexpr int kClipRowCount = 8;

  // Content width of one track's own column; one divider column follows
  // each. Shared between render() and startClipRename() so the reader's
  // own placement always lines up with what render() just drew.
  static constexpr int kColWidth = 18;

  // The row-axis kind a given *logical* (cursor-addressable) row is -
  // physicalRowFor()/kLogicalToPhysical map between this and the actual
  // on-screen row, which also includes non-addressable divider/label rows
  // in between (see kLogicalToPhysical's own comment). Mute/Solo aren't
  // part of this row axis at all - they're shown inline on the header row
  // instead (see this class's own header comment) since they're a
  // per-track toggle, not something that needs its own cursor-addressable
  // slot - but the header row itself *is* cursor-addressable (its own
  // RowKind, HEADER), for its own name to be reachable by cursor
  // navigation the same way every other row is (F2 there renames the
  // track - see startTrackRename()), rather than needing a separate
  // out-of-band gesture to reach it.
  enum class RowKind { HEADER, CLIP, SENDS, DIRECTION };
  // logical row index -> physical row offset (0 = the row right after the
  // header; -1 is the header row itself, always drawn at screen row 0
  // regardless of scroll_row_ - see ensureCursorVisible()'s own handling
  // of a negative cursor_physical) - the fixed layout this class always
  // draws: the header, 8 clip rows, divider, Sends label + Sends value,
  // divider, Direction label + Direction value. Only the *value* row of
  // Sends/Direction is cursor-addressable - the label row above it (the
  // "description above each value" this class's own header comment
  // mentions) is purely decorative, matching how a divider row is.
  static constexpr int kLogicalToPhysical[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 10, 13 };
  static constexpr int kLogicalRowCount = sizeof(kLogicalToPhysical) / sizeof(kLogicalToPhysical[0]);
  static constexpr int kPhysicalRowCount = 14; // total rows below the header, fixed regardless of song content
  RowKind rowKindFor(int logical_row) const;

  int cursor_track_index_ = 0;
  int cursor_row_ = 1; // logical row index - see kLogicalToPhysical's own comment; starts on the first clip row, not the header
  int scroll_col_ = 0, scroll_row_ = 0; // scroll_row_ is in *physical* row space, like the on-screen content itself

  int current_song_version_ = -1;
  int current_cursor_track_index_ = -1, current_cursor_row_ = -1;
  int current_scroll_col_ = -1, current_scroll_row_ = -1;
  bool current_focused_ = false;
  std::string current_focused_clip_id_;
  // Session recording arms/disarms with no song version bump of its own
  // (nothing about the song's own data changes until a take actually
  // produces something) - tracked here so render()'s own dirty-check
  // still notices the record indicator (see its own drawing code) needing
  // to appear or disappear.
  bool current_session_recording_ = false;
  int current_session_recording_track_id_ = -1, current_session_recording_clip_index_ = -1;
  // Set whenever something changed that render()'s own dirty-check above
  // wouldn't otherwise notice - specifically, closing the rename reader
  // without committing (Ctrl-g): no song version bump happens then, but
  // the reader's own screen real estate still needs a fresh paint to
  // clear it. Mirrors ArrangementGrid::force_redraw_ exactly, same reason.
  bool force_redraw_ = false;

  // startClipRename()'s only source of a StyleProvider to force an
  // immediate repaint with - see its own comment on why. Mirrors
  // PatternEditor::last_styles_ exactly, same reason.
  const StyleProvider * last_styles_ = nullptr;

  std::function<void(int track_id, int clip_index)> trigger_callback_;

  // Which clip (song-wide clip list index, i.e. Song::getClips(track_id)
  // position) startClipRename() is currently editing the name of, or -1
  // when the reader isn't open for a rename at all - mirrors
  // ArrangementGrid::renaming_scene_idx_'s own shape. The clip's own
  // track_id is always cursor_track_index_'s own track at the moment
  // the rename started (rename never survives a track/column change,
  // matching startClipRename()'s own early "reader already active" guard
  // against reopening one mid-edit).
  int renaming_clip_row_ = -1;

  // The track startTrackRename() is currently editing the name of
  // (internal id, not column index - stable even if tracks get
  // reordered mid-edit, though that can't actually happen while the
  // reader owns input), or -1 when it isn't open. Mutually exclusive
  // with renaming_clip_row_ - readerActive() only ever allows one reader
  // open at a time.
  int renaming_track_id_ = -1;

  void ensureCursorVisible(int visible_rows, int visible_cols, int num_tracks);
  void startClipRename(const Song & song, const std::vector<int> & track_ids);
  // F2 on any row but a populated clip slot renames the track itself
  // instead (see offerInput()'s own F2 handling) - the header row has no
  // cursor-addressable slot of its own to trigger this from directly.
  void startTrackRename(const Song & song, const std::vector<int> & track_ids);
};

#endif
