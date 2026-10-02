#ifndef _CLIPGRID_H_
#define _CLIPGRID_H_

#include "../UIElement.h"
#include "InlineEditor.h"
#include "LevelMeter.h"
#include "../../launchpad/SessionPadHighlight.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class InputEvent;
class StyleProvider;
class Song;

// A per-track overview: one column per color-eligible leaf track (Song::
// getPlayableTrackIds()), each headed by that track's own ordinal number
// and name (F2 renames it - see startTrackRename()) plus its Mute/Solo
// state (an "M"/"S" pair at the header row's own right edge - dim when
// off, colored when on; there's no dedicated Mute/Solo row any more, see
// physicalFor()'s own comment), on its own dark grey backdrop
// (styles.window_accent_bg_color) setting the header apart from the
// plain window background below it, followed by its own
// Song::getClips(track_id) as a vertical list of exactly
// clipRowCount() slots (a leading play glyph plus name for a real clip,
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
// Shown in Session view (UI::View), above PatternEditor - see
// TerminalUI::layout().
class ClipGrid : public UIElement {
 public:
  ClipGrid(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh, bool focused);

  // A clip slot's transport/recording state (LaunchpadManager::
  // clipHighlight()), shown on its row the way a Launchpad pad shows it.
  // Unset, the grid shows no clip states.
  void setClipStateSource(std::function<SessionPadHighlight(int track_id, int clip_index)> source) { clip_state_source_ = std::move(source); }
  // The clip each track is at (its position in the pattern editor below) -
  // marked, faintly, in that track's own column. Unset, nothing is.
  void setTrackClipSource(std::function<int(int track_id)> source) { track_clip_source_ = std::move(source); }
  bool offerInput(const InputEvent & input) override;
  // See PatternEditor::isReaderActive()/cancelReaderEdit().
  bool isReaderActive() const { return inline_editor_.isOpen(); }
  void cancelReaderEdit() { inline_editor_.cancel(); }

  // Lets UI move the column cursor to the shared current track
  // (TerminalUI::syncSessionView()).
  void setCursorTrackIndex(int track_index) { cursor_track_index_ = track_index; }

  // Read-only counterparts, for Controller::setClipGridCursor() (kept
  // current by UI::renderComponents() every frame) - the column index
  // among Song::getPlayableTrackIds() the cursor is currently on, and its
  // own clip-list index (Song::getClips(track_id)) when the cursor is on
  // a real clip row at all (-1 otherwise - the header, or the Sends/
  // Direction rows, name no clip slot). Mirrors delete-clip's own
  // identical "a CLIP row's own physical offset doubles as its clip-list
  // index" resolution (see rowKindFor()/physicalFor()'s own comment).
  int getCursorTrackIndex() const { return cursor_track_index_; }
  int getCursorClipIndex() const { return rowKindFor(cursor_row_) == RowKind::CLIP ? physicalFor(cursor_row_) : -1; }
  // Moves the cursor onto clip row `clip_index` (the scene), keeping its
  // track.
  void setCursorClipIndex(int clip_index) { cursor_row_ = 1 + std::max(clip_index, 0); }

  // Called on Enter over a clip row (populated or not) with the row's own
  // (track_id, clip_index) - Enter acts exactly like a Launchpad Session
  // view pad press landing on that same cell (SessionPlayer::
  // triggerClip()), never a separate "focus for editing" gesture
  // of its own, the same callback-not-reaching-into-UI pattern
  // ArrangementGrid's own commit_callback_ already uses (this class has
  // no idea LaunchpadManager exists either). Wired in UI::start().
  void setTriggerCallback(std::function<void(int track_id, int clip_index)> cb) { trigger_callback_ = std::move(cb); }
  // The master column (the last one, after every track): Enter on one of
  // its clip rows launches that whole scene, on its Stop row stops every
  // track.
  void setSceneCallback(std::function<void(int clip_index)> cb) { scene_callback_ = std::move(cb); }
  void setStopAllCallback(std::function<void()> cb) { stop_all_callback_ = std::move(cb); }
  // See handleMouse(): whether a left-button press is still held.
  bool isMouseDown() const { return mouse_down_; }
  void releaseMouse() { mouse_down_ = false; }
  // Rows needed to show everything without scrolling (the header plus
  // every row below it).
  int preferredHeight() const { return 1 + physicalRowCount(); }

 private:
  // Clip rows per column: one per scene (ScenePatternSource::
  // sceneCount()) - the same for every column, whether or not that many
  // clips actually exist on the track, so every track's own row axis is
  // identical.
  int clipRowCount() const;

  // Content width of one track's own column; one divider column follows
  // each. Shared between render() and startClipRename() so the reader's
  // own placement always lines up with what render() just drew.
  static constexpr int kColWidth = 18;
  // The level meter: a vertical bar in a column's last cell,
  // beside the Sends/Direction rows (kSendsLabel through kDirectionValue),
  // which leave that cell free.
  static constexpr int kMeterRows = 5;
  static constexpr int kSendsTextWidth = kColWidth - 1;

  // The row-axis kind a given *logical* row is - physicalFor() maps
  // between this and the actual on-screen row, which also includes
  // non-addressable divider/label rows in between. Mute/Solo aren't part
  // of this row axis at all - they're shown inline on the header row
  // instead (see this class's own header comment). The header row is
  // logical row 0 but never holds the cursor; F2 away from a clip renames
  // the track instead (see offerInput()).
  enum class RowKind { HEADER, CLIP, SENDS, DIRECTION };
  // logical row index -> physical row offset (0 = the row right after the
  // header; -1 is the header row itself, always drawn at screen row 0
  // regardless of scroll_row_ - see ensureCursorVisible()'s own handling
  // of a negative cursor_physical) - the layout this class always draws:
  // the header, clipRowCount() clip rows, then (as offsets from there,
  // SendsDirectionRow) divider, Sends label + Sends value, divider,
  // Direction label + Direction value. Only the *value* row of
  // Sends/Direction is cursor-addressable - the label row above it (the
  // "description above each value" this class's own header comment
  // mentions) is purely decorative, matching how a divider row is.
  enum SendsDirectionRow { kSendsDivider, kSendsLabel, kSendsValue, kDirectionDivider, kDirectionLabel, kDirectionValue, kSendsDirectionRows };
  int physicalFor(int logical_row) const;
  int logicalRowCount() const { return clipRowCount() + 3; }
  int physicalRowCount() const { return clipRowCount() + kSendsDirectionRows; } // every row below the header
  RowKind rowKindFor(int logical_row) const;

  int cursor_track_index_ = 0;
  int cursor_row_ = 1; // logical row index - see physicalFor()'s own comment; starts on the first clip row, not the header
  int scroll_col_ = 0, scroll_row_ = 0; // scroll_row_ is in *physical* row space, like the on-screen content itself

  int current_song_version_ = -1;
  int current_cursor_track_index_ = -1, current_cursor_row_ = -1;
  int current_scroll_col_ = -1, current_scroll_row_ = -1;
  // Set by the mouse wheel, which scrolls the view without the cursor;
  // cleared when the cursor next moves.
  bool view_detached_ = false;
  bool current_focused_ = false;
  std::string current_focused_clip_id_;
  // Session recording arms/disarms with no song version bump of its own
  // (nothing about the song's own data changes until a take actually
  // produces something) - tracked here so render()'s own dirty-check
  // still notices the record indicator (see its own drawing code) needing
  // to appear or disappear.
  std::function<SessionPadHighlight(int track_id, int clip_index)> clip_state_source_;
  std::function<int(int track_id)> track_clip_source_;
  // Each track's clip (track_clip_source_) at the last redraw - a change
  // redraws.
  std::vector<int> current_track_clips_;
  // The visible clip slots' states at the last redraw - a change redraws.
  std::vector<SessionPadHighlight> current_clip_states_;

  std::function<void(int track_id, int clip_index)> trigger_callback_;
  std::function<void(int clip_index)> scene_callback_;
  std::function<void()> stop_all_callback_;
  // The visible meters' levels and peak markers at the last redraw, in bar
  // steps (two entries per column) - a change redraws.
  std::vector<int> current_meter_steps_;
  // What a column's meter shows now, and the smoothing behind it, by track
  // id (the master's included).
  struct MeterDisplay {
    level_meter::Ballistics ballistics;
    level_meter::PeakHold peak_hold;
    float fraction = 0.0f;
    float peak_fraction = 0.0f;
  };
  std::unordered_map<int, MeterDisplay> meters_;
  std::chrono::steady_clock::time_point last_meter_update_;

  // Clip and track rename share one editor; only one can be open at a time.
  InlineEditor inline_editor_{getPlane()};

  void ensureCursorVisible(int visible_rows, int visible_cols, int num_tracks);
  void renderMasterColumn(const StyleProvider & styles, int x, int rows, bool focused, int num_tracks,
                          const std::function<SessionPadHighlight(int clip_row)> & scene_state);
  // A column's level meter and peak marker in its last cell, over
  // whichever of its kMeterRows rows are on screen.
  void renderMeter(const StyleProvider & styles, int x, int rows, int track_id, bool clipping);
  // Edits the Send Main/A/B of the column under the cursor - a track's,
  // or the master's (the dry mix and the send bus's returns).
  void startSendsEdit(int track_id);
  // What Enter (or a click, which never opens the Sends editor) does on the
  // cell under the cursor.
  void activateCell(const Song & song, const std::vector<int> & track_ids, bool edit_sends);
  // A left-button press or release: picks the cell under it, and a clip
  // slot (or the master's scene/stop-all slots) acts as a button.
  bool handleMouse(const InputEvent & input);
  // Between a press and its release - see handleMouse().
  bool mouse_down_ = false;
  void startClipRename(const Song & song, const std::vector<int> & track_ids);
  // F2 on any row but a populated clip slot renames the track itself
  // instead (see offerInput()'s own F2 handling) - the header row has no
  // cursor-addressable slot of its own to trigger this from directly.
  void startTrackRename(const Song & song, const std::vector<int> & track_ids);
};

#endif
