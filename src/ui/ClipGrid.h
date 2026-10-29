#ifndef _CLIPGRID_H_
#define _CLIPGRID_H_

#include "UIElement.h"
#include "InlineEditor.h"
#include "LevelMeter.h"

#include <cellui/Dial.h>
#include <cellui/Fade.h>
#include <cellui/Scopes.h>
#include "../launchpad/ClipHighlight.h"
#include "../model/Clip.h"
#include "../model/SendLevels.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <cellui/Input.h>
#include "StyleProvider.h"
class Song;

// A per-track overview: one column per color-eligible leaf track (Song::
// getPlayableTrackIds()), each headed by that track's own ordinal number
// and name (the rename command renames it - see startTrackRename()) plus its Mute/Solo
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
// Shown in Live View (UI::View), above PatternEditor - see
// InteractiveUI::layout().
class ClipGrid : public UIElement {
 public:
  ClipGrid(Plane & parent);

  bool render(const StyleProvider & styles, bool refresh, bool focused);

  // The backend's scopes, which draw the VU meters; unset, they are drawn
  // in cells.
  void setScopes(const cellui::Scopes * scopes) { scopes_ = scopes; }

  // A clip slot's transport/recording state (LaunchpadManager::
  // clipHighlight()), shown on its row the way a Launchpad pad shows it.
  // Unset, the grid shows no clip states.
  void setClipStateSource(std::function<ClipHighlight(int track_id, int clip_index)> source) { clip_state_source_ = std::move(source); }
  // The clip each track is at (its position in the pattern editor below) -
  // marked, faintly, in that track's own column. Unset, nothing is.
  void setTrackClipSource(std::function<int(int track_id)> source) { track_clip_source_ = std::move(source); }
  bool offerInput(const cellui::Input & input) override;
  // See PatternEditor::isReaderActive()/cancelReaderEdit().
  bool isReaderActive() const { return inline_editor_.isOpen() || isDialEditing(); }
  // A bare Escape closes an open editor at once.
  bool wantsBareEscape() const override { return isReaderActive(); }
  // The focus level is still easing: the loop keeps drawing frames.
  bool isAnimating() const { return fades_.active() || focus_fade_drawn_; }
  void cancelReaderEdit() {
    inline_editor_.cancel();
    for (auto & [key, dial] : dials_) dial->cancelEdit();
  }

  // Lets UI move the column cursor to the shared current track
  // (InteractiveUI::syncLiveView()).
  void setCursorTrackIndex(int track_index) { cursor_track_index_ = track_index; }

  // Read-only counterparts, for Controller::setClipGridCursor() (kept
  // current by UI::renderComponents() every frame) - the column index
  // among Song::getPlayableTrackIds() the cursor is currently on, and its
  // own clip-list index (Song::getClips(track_id)) when the cursor is on
  // a real clip row at all (-1 otherwise - the header, or the Sends/
  // Direction rows, name no clip slot). Mirrors kill-region's own
  // identical "a CLIP row's own physical offset doubles as its clip-list
  // index" resolution (see rowKindFor()/physicalFor()'s own comment).
  int getCursorTrackIndex() const { return cursor_track_index_; }
  int getCursorClipIndex() const { return rowKindFor(cursor_row_) == RowKind::CLIP ? physicalFor(cursor_row_) : -1; }
  // Moves the cursor onto clip row `clip_index` (the scene), keeping its
  // track.
  void setCursorClipIndex(int clip_index) { cursor_row_ = 1 + std::max(clip_index, 0); }

  // Called on Enter over a clip row (populated or not) with the row's own
  // (track_id, clip_index) - Enter acts exactly like a Launchpad Live
  // view pad press landing on that same cell (ClipPlayer::
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
  static constexpr int kColWidth = 16;
  // A dial's slot: kDialWidth cells by kDialRows rows. A and B
  // stand side by side, A at kDialX in the column and B kDialStride cells
  // further on; Main is centred, at kMainX. The column's last cell is the
  // meter's.
  static constexpr int kDialX = 1, kDialStride = 8, kDialWidth = 5, kDialRows = 3;
  static constexpr int kMainX = (kColWidth - 1 - kDialWidth) / 2;
  // The level meter: a vertical bar in a column's last cell, beside the Main
  // and Direction rows (kMainTop through kDirectionValue); the Sends rows
  // above leave that cell free.
  static constexpr int kMeterRows = 6;

  // The row-axis kind a given *logical* row is - physicalFor() maps
  // between this and the actual on-screen row, which also includes
  // non-addressable divider/label rows in between. Mute/Solo aren't part
  // of this row axis at all - they're shown inline on the header row
  // instead (see this class's own header comment). The header row is
  // logical row 0 but never holds the cursor; rename away from a clip renames
  // the track instead (see offerInput()).
  enum class RowKind { HEADER,
                       CLIP,
                       SENDS,
                       MAIN,
                       DIRECTION };
  // logical row index -> physical row offset (0 = the row right after the
  // header; -1 is the header row itself, always drawn at screen row 0
  // regardless of scroll_row_ - see ensureCursorVisible()'s own handling
  // of a negative cursor_physical) - the layout this class always draws:
  // the header, clipRowCount() clip rows, then (as offsets from there,
  // SendsDirectionRow) a divider and the four rows of the Sends group (the
  // A dial on its first three, the B dial on its last three, one row lower,
  // the caption above B), a divider, the three rows of the Main dial, a
  // divider, and the Direction label and value. A group is one cursor stop, standing on its
  // last row (physicalFor()); the dividers and the Direction label are
  // decorative.
  enum SendsDirectionRow {
    kSendsDivider,
    kSendsTop,
    kSendsBTop,
    kSendsMiddle,
    kSendsBottom,
    kMainDivider,
    kMainTop,
    kMainMiddle,
    kMainBottom,
    kDirectionDivider,
    kDirectionLabel,
    kDirectionValue,
    kSendsDirectionRows
  };
  int physicalFor(int logical_row) const;
  int logicalRowCount() const { return clipRowCount() + 4; }
  int physicalRowCount() const { return clipRowCount() + kSendsDirectionRows; } // every row below the header
  RowKind rowKindFor(int logical_row) const;

  int cursor_track_index_ = 0;
  // Which of the Sends row's dials the cursor is on (0: A, 1: B).
  int cursor_send_ = 0;
  int cursor_row_ = 1; // logical row index - see physicalFor()'s own comment; starts on the first clip row, not the header
  int scroll_col_ = 0, scroll_row_ = 0; // scroll_row_ is in *physical* row space, like the on-screen content itself

  int current_song_version_ = -1;
  int current_cursor_track_index_ = -1, current_cursor_row_ = -1, current_cursor_send_ = -1;
  int current_scroll_col_ = -1, current_scroll_row_ = -1;
  // Set by the mouse wheel, which scrolls the view without the cursor;
  // cleared when the cursor next moves.
  bool view_detached_ = false;
  bool current_focused_ = false;
  std::string current_focused_clip_id_;
  // clip recording arms/disarms with no song version bump of its own
  // (nothing about the song's own data changes until a take actually
  // produces something) - tracked here so render()'s own dirty-check
  // still notices the record indicator (see its own drawing code) needing
  // to appear or disappear.
  std::function<ClipHighlight(int track_id, int clip_index)> clip_state_source_;
  // The last clip killed or copied, and the kind of track it came from -
  // a yank only lands on a track of the same kind (note values mean
  // different things under different tunings, audio only fits audio).
  std::optional<Clip> clip_clipboard_;
  int clip_clipboard_track_type_ = -1;
  std::function<int(int track_id)> track_clip_source_;
  // Each track's clip (track_clip_source_) at the last redraw - a change
  // redraws.
  std::vector<int> current_track_clips_;
  // The visible clip slots' states at the last redraw - a change redraws.
  // How focused the grid is, 0..1, eased over a few frames.
  float focus_ = 0.0f;
  cellui::Fades fades_;
  bool focus_fade_drawn_ = false;
  std::vector<ClipHighlight> current_clip_states_;

  std::function<void(int track_id, int clip_index)> trigger_callback_;
  std::function<void(int clip_index)> scene_callback_;
  std::function<void()> stop_all_callback_;
  // The visible meters' levels and peak markers at the last redraw, in
  // 1/256ths of the meter (two entries per column) - a change redraws.
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
  const cellui::Scopes * scopes_ = nullptr;
  std::chrono::steady_clock::time_point last_meter_update_;

  // Clip and track rename share one editor; only one can be open at a time.
  InlineEditor inline_editor_{getPlane()};

  void ensureCursorVisible(int visible_rows, int visible_cols, int num_tracks);
  void renderMasterColumn(const StyleProvider & styles, int x, int rows, bool focused, int num_tracks,
                          const std::function<ClipHighlight(int clip_row)> & scene_state);
  // A column's Sends and Main dials, from the track's (or the master's) send
  // levels.
  void renderDials(const StyleProvider & styles, int x, int rows, int column, int track_id, const SendLevels & sends);
  // One row of the Sends or Main group (`offset` rows below the clip rows):
  // its background, and the Sends caption.
  std::string dividerLine(const StyleProvider & styles, int offset) const;
  void putDialGroupRow(const StyleProvider & styles, int y, int x, int offset);
  // A column's level meter and peak marker in its last cell, over
  // whichever of its kMeterRows rows are on screen.
  void renderMeter(const StyleProvider & styles, int x, int rows, int track_id, bool clipping);
  // Edits the send of the column under the cursor - 0: Main, 1: A, 2: B - a
  // track's, or the master's (the dry mix and the send bus's returns).
  void putDivider(const StyleProvider & styles, int y, int x, bool header);
  // What Enter or a click does on the
  // cell under the cursor.
  void activateCell(const Song & song, const std::vector<int> & track_ids);
  // A left-button press or release: picks the cell under it, and a clip
  // slot (or the master's scene/stop-all slots) acts as a button.
  bool handleMouse(const cellui::Input & input);
  // Between a press and its release - see handleMouse().
  bool mouse_down_ = false;
  // The cell a held button is on, shown pressed: the release picks it. `column`
  // is -1 when nothing is pressed, `physical` -1 for the header row and -2 for a
  // row that only picks the track (a label, a divider).
  struct Pressed {
    int column = -1, logical_row = 0, physical = -2, send = -1; // send: the dial on a Sends row
    bool operator==(const Pressed & o) const { return column == o.column && logical_row == o.logical_row && send == o.send; }
    bool operator!=(const Pressed & o) const { return !(*this == o); }
  };
  Pressed pressed_, current_pressed_;
  // The dials of every column, by track id and send (0 Main, 1 A, 2 B): each a
  // cellui::Dial that turns, types and draws itself; the grid places them, gives
  // them their values and passes on the mouse and keys.
  std::unordered_map<uint64_t, std::unique_ptr<cellui::Dial>> dials_;
  uint64_t current_dials_state_ = 0;
  cellui::Dial & dialAt(int track_id, int send);
  void selectDial(int track_id, int send);
  cellui::Dial * cursorDial();
  bool isDialEditing() const;
  bool dialMouse(const cellui::Input & input);
  // What is under the screen position, if it is a column.
  Pressed hitTest(int screen_y, int screen_x, int num_tracks) const;
  bool isPressed(int column, int physical) const { return pressed_.column == column && pressed_.physical == physical; }
  // The "rename" command: the scene, clip or track under the cursor.
  void startRename();
  void startClipRename(const Song & song, const std::vector<int> & track_ids);
  // "rename" on the master column's scene slot edits the scene's name.
  void startSceneRename();
  // "rename" on any row but a populated clip slot renames the track itself
  // instead (see startRename()) - the header row has no
  // cursor-addressable slot of its own to trigger this from directly.
  void startTrackRename(const Song & song, const std::vector<int> & track_ids);
};

#endif
