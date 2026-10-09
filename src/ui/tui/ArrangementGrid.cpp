#include "ArrangementGrid.h"

#include "../../playback/InputEvent.h"
#include "../../playback/LogEvent.h"
#include "../StyleProvider.h"
#include "../../Controller.h"
#include "../../model/Song.h"
#include "../../model/Arrangement.h"
#include "../../model/Clip.h"
#include "../../model/ArrangementOps.h"
#include "../../model/SongStructure.h"
#include "../KeyChord.h"

#include <algorithm>
#include <fmt/core.h>

using namespace std;

vector<int>
ArrangementGrid::getVisibleTrackIds(const Song & song) const {
  return song.getPlayableTrackIds();
}

int
ArrangementGrid::barCount(const Song & song, int playing_bar) const {
  // The arrangement's length is a bar start, so its index counts the bars before it.
  auto bars = song.getArrangementBars().barIndex(song.getArrangementLength());
  return max({ bars + 1, cursor_bar_ + 1, playing_bar + 1 });
}

int
ArrangementGrid::getCursorRow(const Song & song) const {
  return song.getArrangementBars().barStartRow(cursor_bar_);
}

void
ArrangementGrid::moveCursorBar(int delta) {
  cursor_bar_ = clamp(cursor_bar_ + delta, 0, Song::kMaxArrangementRows - 1);
}

ArrangementGrid::ArrangementGrid(UIPlane & parent) : UIElement(parent) {
  // Deliberate no-ops, not left undefined: a Launchpad's CC91/92 ("move-
  // row-up"/"move-row-down") reach here via UI::executeCommand()'s
  // active-element-first dispatch while this widget has focus - without
  // an entry of its own, that dispatch falls through to PatternEditor's
  // *own* "move-row-up"/"move-row-down" (its own fallback for a command
  // the active element doesn't own), which would move the real playhead
  // even though this widget's own cursor is meant to stay local and
  // passive. Defining (even empty) commands here absorbs the dispatch
  // instead.
  commands_.define("move-row-up", []() {});
  commands_.define("move-row-down", []() {});

  assertCommandBindingsValid();
}

void
ArrangementGrid::ensureCursorVisible(int bar_count, int visible_rows, int visible_cols, int num_tracks, bool follow_cursor) {
  cursor_bar_ = clamp(cursor_bar_, 0, bar_count - 1);
  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));

  // Always kept in bounds (a shrunk song must never leave a stale
  // out-of-range scroll position behind), regardless of follow_cursor.
  scroll_row_ = clamp(scroll_row_, 0, max(0, bar_count - visible_rows));
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));

  // Not focused - render() keeps the playhead in view instead (see its
  // own comment on why only one of the two ever drives the scroll
  // position in a given frame).
  if (!follow_cursor) return;

  if (cursor_bar_ < scroll_row_) scroll_row_ = cursor_bar_;
  if (visible_rows > 0 && cursor_bar_ >= scroll_row_ + visible_rows) scroll_row_ = cursor_bar_ - visible_rows + 1;
  scroll_row_ = clamp(scroll_row_, 0, max(0, bar_count - visible_rows));

  if (cursor_track_index_ < scroll_col_) scroll_col_ = cursor_track_index_;
  if (visible_cols > 0 && cursor_track_index_ >= scroll_col_ + visible_cols) scroll_col_ = cursor_track_index_ - visible_cols + 1;
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));
}

bool
ArrangementGrid::offerInput(const InputEvent & input) {
  // A left click picks the (bar, track) cell under it; Enter still commits it.
  if (input.getId() == NCKEY_BUTTON1) {
    if (input.getKind() == InputEvent::Kind::RELEASE) return true;
    auto & song = getController().getSong();
    auto num_tracks = static_cast<int>(getVisibleTrackIds(song).size());
    auto [ pos_y, pos_x ] = getPosition();
    auto [ rows, cols ] = getDim();
    auto y = input.getY() - pos_y, x = input.getX() - pos_x;
    if (y < 0 || y >= rows || x < 0 || x >= cols) return false;
    constexpr int kColWidth = 2; // see render()
    auto track = scroll_col_ + x / kColWidth;
    if (track >= num_tracks || x >= max(0, cols - 2) / kColWidth * kColWidth) return true; // the locator column, or past the last track
    auto playing_bar = song.getArrangementBars().barIndex(getController().getPlaybackInfo().getAbsolutePosition());
    auto bar = scroll_row_ + y;
    if (bar >= barCount(song, playing_bar)) return true; // below the last bar
    cursor_track_index_ = track;
    cursor_bar_ = bar;
    view_detached_ = false;
    return true;
  }

  // The mouse wheel scrolls the view (Shift: tracks), not the cursor, and
  // detaches it from the cursor/playhead until the cursor next moves.
  if (input.getId() == NCKEY_BUTTON4 || input.getId() == NCKEY_BUTTON5) {
    if (input.getKind() == InputEvent::Kind::RELEASE) return true;
    int direction = input.getId() == NCKEY_BUTTON4 ? -1 : 1;
    if (input.hasShift()) scroll_col_ += direction;
    else scroll_row_ += direction;
    view_detached_ = true;
    return true;
  }

  if (dispatchCommand(input)) return true;
  if (input.getKind() == InputEvent::Kind::RELEASE) return false;

  auto & song = getController().getSong();
  auto track_ids = getVisibleTrackIds(song);
  auto num_tracks = static_cast<int>(track_ids.size());
  auto bars = song.getArrangementBars();
  auto bar_row = [&](int bar) { return bars.barStartRow(bar); };
  auto bar_length = [&](int bar) { return bars.nextBarStart(bars.barStartRow(bar)) - bars.barStartRow(bar); };

  // The cursor moves over the arrangement's bars and one empty one past
  // them (barCount()).
  auto move_cursor = [&](int delta) {
    auto playing_bar = bars.barIndex(getController().getPlaybackInfo().getAbsolutePosition());
    cursor_bar_ = clamp(cursor_bar_ + delta, 0, barCount(song, playing_bar) - 1);
  };

  if (input.getId() == NCKEY_ENTER) {
    // A bar past the arrangement's end is a valid target: it just moves
    // the playhead/track selection there - "look at/jump to this
    // position" and "put real content here" stay two separate actions.
    if (commit_callback_ && cursor_track_index_ < num_tracks) {
      commit_callback_(track_ids[static_cast<size_t>(cursor_track_index_)], bar_row(cursor_bar_));
    }
    return true;
  }

  if (input.getId() == NCKEY_UP) move_cursor(-1);
  else if (input.getId() == NCKEY_DOWN) move_cursor(1);
  // A full screenful at a time - same "jump by the viewport's own
  // height" reading Page Up/Down carry everywhere else in this app,
  // rather than PatternEditor's own fixed 16-row jump (which counts raw
  // pattern rows, not this grid's own bar rows, so a fixed count
  // wouldn't mean the same thing here).
  else if (input.getId() == NCKEY_PGUP) move_cursor(-getDim().first);
  else if (input.getId() == NCKEY_PGDOWN) move_cursor(getDim().first);
  else if (input.getId() == NCKEY_BACKSPACE) {
    if (cursor_track_index_ < num_tracks) {
      auto arrangement = song.getArrangement();
      auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
      auto bar_start_row = bar_row(cursor_bar_);
      auto active = resolveInstanceForBar(song, track_id, bar_start_row, bar_length(cursor_bar_));
      if (active.clip_index >= 0 && active.start_row >= bar_start_row) {
        // On the instance's own leading (head) bar - removes the placement
        // event, leaving a stop in its place when an earlier looping clip
        // would otherwise play on into the space (removeInstanceLeavingSilence()).
        // The clip itself is untouched, still in the track's own clip list -
        // this only ends this one placement of it.
        Song::Edit edit(song, "end clip placement");
        removeInstanceLeavingSilence(song, track_id, active.start_row);
      } else if (active.clip_index >= 0) {
        // A later (tail) bar the same instance merely continues through -
        // no single event's own row to remove here, only a stop can
        // truncate/mark it, landing at this bar's own row regardless of
        // wherever the instance being truncated actually started.
        Song::Edit edit(song, "place stop");
        placeStopInstance(song, track_id, bar_start_row);
      }
      // Else: this bar was already silent (an earlier stop already
      // applies here, or nothing was ever placed at all) - nothing to
      // cut, so nothing to do. A clip is started once and stopped once;
      // once stopped, it never plays again in any later bar either, so
      // placing another stop on an already-silent bar would only
      // duplicate the one that already does the job.
    }
  }
  else if (input.getId() == NCKEY_DEL || (input.hasCtrl() && input.getId() == 'k')) {
    // Fully removes the clip itself (ArrangementOps.h's deleteClip()) -
    // every placement of it anywhere in the song, not just the one under
    // the cursor - unlike Backspace above, which only ends this one
    // placement going forward and leaves the clip itself in the track's
    // own clip list, reusable elsewhere. Matches the same Del/Ctrl-K ->
    // delete-clip convention Live View's own clip list already uses.
    if (cursor_track_index_ < num_tracks) {
      auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
      auto active = resolveInstanceForBar(song, track_id, bar_row(cursor_bar_), bar_length(cursor_bar_));
      if (active.clip_index >= 0) {
        auto clips = song.getClips(track_id);
        auto clip_id = clips[static_cast<size_t>(active.clip_index)].getId();
        auto name = clips[static_cast<size_t>(active.clip_index)].getName();
        // Same "clear a stale focus rather than leave it dangling" reasoning
        // ClipGrid's own delete-clip already has.
        if (getController().getFocusedClipTrackId() == track_id && getController().getFocusedClip() == clip_id) {
          getController().clearFocusedClip();
        }
        deleteClip(song, track_id, active.clip_index);
        auto text = "Deleted clip: " + (name.empty() ? string("(unnamed)") : name);
        getController().getUIEventQueue().push(make_unique<LogEvent>(std::move(text)));
      } else if (active.clip_index == Arrangement::kStopInstance) {
        // No clip to delete - just the stop event itself
        // (Arrangement::clearInstance(), not placeStopInstance() with some
        // other value - there's nothing to replace it with, only to take
        // away). Removing it, not overwriting it with another stop or
        // leaving it in place, restores whatever's actually still active
        // from before it (an earlier instance, or the background) rather
        // than forcing silence to persist here - the same "delete
        // reverts to whatever's underneath" semantics deleting a clip
        // already has.
        Song::Edit edit(song, "delete stop");
        song.getArrangement().clearInstance(track_id, active.start_row);
        getController().getUIEventQueue().push(make_unique<LogEvent>("Deleted stop"));
      }
    }
  }
  // Left/Right move across per-track columns, stopping at the grid's
  // own edges.
  else if (input.getId() == NCKEY_LEFT) cursor_track_index_--;
  else if (input.getId() == NCKEY_RIGHT) cursor_track_index_++;
  else return false;

  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));

  // Song::getCurrentTrackId() sync - see PatternEditor::render()'s own
  // equivalent for why this matters (a command like merge-clip-to-
  // background needs to find the right track regardless of which widget
  // actually moved the cursor last).
  if (cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) song.setCurrentTrackId(track_ids[static_cast<size_t>(cursor_track_index_)]);

  return true;
}

namespace {

// Whether `track_id`'s own background Pattern has anything defined
// across [raw_row, raw_row + rows_per_bar) - the fallback shown for a
// bar with no active clip instance. `has_sounding_note` tells a real
// note-on apart from a bar that's non-empty purely from note-offs/
// aftertouch/Command data (Pattern::hasSoundingNote()'s own distinction,
// applied per-bar instead of to a whole Pattern).
bool barHasBackgroundContent(const ArrangementView & arrangement, int track_id, int raw_row, int rows_per_bar, bool & has_sounding_note) {
  bool has_any = false;
  has_sounding_note = false;
  for (int row = raw_row; row < raw_row + rows_per_bar; row++) {
    auto effective_row = arrangement.getEffectiveRow(track_id, row, 0);
    for (auto & note : arrangement.getNotes(effective_row, track_id)) {
      if (note.isDefined()) {
        has_any = true;
        if (!note.isOff() && !note.isAftertouch()) has_sounding_note = true;
      }
    }
    if (arrangement.getCommand(effective_row, track_id).isDefined()) has_any = true;
  }
  return has_any;
}

// Whether `track_id` has a literal stop event of its own stored somewhere
// in [raw_row, raw_row + rows_per_bar) - a direct lookup against the raw
// instance data, independent of resolveInstanceForBar()'s own "what
// governs playback here" resolution (which a stop, once placed, keeps
// answering for every later bar too). The marker glyph is drawn purely
// because a stop is actually on this line, not because playback is
// currently stopped here.
bool barHasOwnStop(const ArrangementView & arrangement, int track_id, int raw_row, int rows_per_bar) {
  auto instances = arrangement.getInstancesForTrack(track_id);
  auto it = instances.lower_bound(static_cast<unsigned short>(raw_row));
  return it != instances.end() && static_cast<int>(it->first) < raw_row + rows_per_bar && it->second == "OFF";
}

}

bool
ArrangementGrid::render(const StyleProvider & styles, bool refresh, bool focused, int selected_track_id) {
  auto & song = getController().getSong();
  auto track_ids = getVisibleTrackIds(song);
  auto num_tracks = static_cast<int>(track_ids.size());
  auto bars = song.getArrangementBars();

  auto [ rows, cols ] = getDim();
  if (rows < 1 || cols < 1) return false;
  // 2 columns per track - the identifier cell itself, plus one shared
  // half-width padding cell (see the render loop's own draw_edge below) -
  // one more standalone padding cell before the first track, for its own
  // left edge, and a locator column last, where the pattern editor shows
  // them too.
  constexpr int kLocatorWidth = 1;
  constexpr int kColWidth = 2;
  auto visible_rows = rows;
  auto visible_cols = max(0, cols - kLocatorWidth - 1) / kColWidth;
  auto locators = song.getLocators();

  // Exactly one of the two ever drives the scroll position in a given
  // frame, never both (letting both run unconditionally, one after the
  // other, does exactly that: whichever ran second always wins outright,
  // which flips again next frame the moment the loser's own position
  // technically differs from what's currently on screen). While actually
  // playing, the transport always wins regardless of focus - watching
  // playback progress is the point, and there's little reason to be
  // navigating this widget's own cursor while the song is moving on its
  // own. Once stopped, focus decides instead: the viewport follows the
  // (local, user-driven) cursor while this widget is focused, same as any
  // other focused/scrollable widget in this app; once focus moves
  // elsewhere (working in PatternEditor, say), the viewport follows the
  // transport position instead - the edit position while stopped,
  // matching PatternEditor's own stopped-row highlight, which isn't gated
  // on isPlaying() either.
  auto & playback_info = getController().getPlaybackInfo();
  auto playing_row = playback_info.getAbsolutePosition();
  auto playing_bar = bars.barIndex(playing_row);
  auto bar_count = barCount(song, playing_bar);
  // A moved cursor reattaches a view the mouse wheel detached - this
  // grid's own, or (while stopped) the edit position it follows unfocused.
  bool edit_position_moved = !playback_info.isPlaying() && playing_row != current_playing_row_;
  if (view_detached_ && (edit_position_moved || cursor_bar_ != current_cursor_bar_ ||
                         cursor_track_index_ != current_cursor_track_index_)) view_detached_ = false;
  auto follow_cursor = !playback_info.isPlaying() && focused && !view_detached_;
  ensureCursorVisible(bar_count, visible_rows, visible_cols, num_tracks, follow_cursor);

  if (!follow_cursor && !view_detached_) {
    if (playing_bar < scroll_row_) scroll_row_ = playing_bar;
    if (visible_rows > 0 && playing_bar >= scroll_row_ + visible_rows) scroll_row_ = playing_bar - visible_rows + 1;
    scroll_row_ = clamp(scroll_row_, 0, max(0, bar_count - visible_rows));
  }

  auto new_version = song.getMajorVersion();

  if (!refresh && new_version == current_song_version_ && playing_row == current_playing_row_ &&
      cursor_bar_ == current_cursor_bar_ &&
      cursor_track_index_ == current_cursor_track_index_ &&
      scroll_row_ == current_scroll_row_ && scroll_col_ == current_scroll_col_ &&
      focused == current_focused_ && selected_track_id == current_selected_track_id_) {
    return false;
  }
  current_song_version_ = new_version;
  current_playing_row_ = playing_row;
  current_cursor_bar_ = cursor_bar_;
  current_cursor_track_index_ = cursor_track_index_;
  current_scroll_row_ = scroll_row_;
  current_scroll_col_ = scroll_col_;
  current_selected_track_id_ = selected_track_id;
  current_focused_ = focused;

  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  erase();

  // Every id in track_ids is already color-eligible (getVisibleTrackIds()
  // filtered out anything that isn't), so every column below has a real
  // identity color to work with.
  SongStructure structure(song);

  // A track's real identity color (VisibleTrackInfo::getColor(), the same
  // one PatternEditor's own heading row paints each track with) - a track
  // reads as the same color everywhere in this app. It's reserved for "an
  // instance is active here" (a colored background, below) - background
  // content with no instance placed over it uses a plain grey glyph
  // instead (styles.window_fg_color, the same grey every other untouched
  // cell's text already uses), uncolored on purpose, so a track's own
  // color never means anything but "a real instance is here."
  auto track_color = [&](int track_id) {
    return structure.getBaselineInfo(track_id).getColor();
  };

  // Per visible column, the active instance (if any) the previous bar row
  // showed for that track - lets the per-track loop below tell "still the
  // same instance, one bar further into it" apart from "a different
  // instance just became active here" without needing to assume an
  // instance's own start_row lands on a bar boundary at all (an instance
  // placed off-grid, e.g. via hand-edited XML, only starts actually
  // resolving on whichever bar's own leading row is the first at or past
  // it - that bar is still its own leading bar for this grid's purposes,
  // even though active.start_row itself doesn't equal that bar's raw_row).
  // Seeded from the bar above the view, so an instance continuing into
  // it doesn't show its digit again.
  vector<int> prev_clip_index(static_cast<size_t>(visible_cols), Arrangement::kNoInstance);
  vector<int> prev_start_row(static_cast<size_t>(visible_cols), -1);
  if (scroll_row_ > 0) {
    for (auto vc = 0; vc < visible_cols && scroll_col_ + vc < num_tracks; vc++) {
      auto above_row = bars.barStartRow(scroll_row_ - 1);
      auto above = resolveInstanceForBar(song, track_ids[static_cast<size_t>(scroll_col_ + vc)], above_row, bars.nextBarStart(above_row) - above_row);
      prev_clip_index[static_cast<size_t>(vc)] = above.clip_index;
      prev_start_row[static_cast<size_t>(vc)] = above.start_row;
    }
  }
  auto arrangement = song.getArrangement();

  for (auto vr = 0; vr < visible_rows; vr++) {
    auto bar = scroll_row_ + vr;
    // Past the bars there are, nothing left to show - still painted
    // explicitly below rather than left to whatever an earlier frame drew
    // in this same screen cell.
    auto in_range = bar < bar_count;
    auto raw_row = bars.barStartRow(bar);
    auto rows_per_bar = bars.nextBarStart(raw_row) - raw_row; // this bar's own length
    auto is_playing_row = in_range && bar == playing_bar;


    // The left edge of the whole row has no neighboring track to blend
    // with, so its own half of the leading padding cell is plain
    // window_bg_color, same as every other untouched cell - the right
    // edge (after the loop below) uses the same reasoning.
    Color prev_bg = styles.window_bg_color;

    for (auto vc = 0; vc < visible_cols; vc++) {
      auto track_index = scroll_col_ + vc;
      // Each track is its own identifier cell (x = pad_x + 1) plus one
      // shared padding cell right before it (x = pad_x) - drawn as a
      // half-block ("▌", U+258C) whose left half paints in the previous
      // track's own color and whose right half paints in this track's
      // own color (the block glyph's foreground fills the left half, its
      // background shows through the right half), so a clip instance
      // reads as a two-cell-wide capsule - half padding, a full
      // identifier cell, half padding - the same way PatternEditor's own
      // renderHeading() draws a boundary between two adjacent heading
      // colors (see its draw_edge()).
      auto pad_x = vc * kColWidth;
      auto digit_x = pad_x + 1;

      string glyph = " ";
      Color fg = styles.window_fg_color, bg = styles.window_bg_color;
      int cur_clip_index = Arrangement::kNoInstance, cur_start_row = -1;

      if (in_range && track_index < num_tracks) {
        auto track_id = track_ids[static_cast<size_t>(track_index)];
        // Bar-granularity, not resolveInstanceAt(raw_row) directly - a
        // short one-shot instance that both starts and finishes again
        // entirely inside this one bar's own row span would otherwise
        // never touch any bar's plain per-row sample at all (see
        // resolveInstanceForBar()'s own comment).
        auto active = resolveInstanceForBar(song, track_id, raw_row, rows_per_bar);
        cur_clip_index = active.clip_index;
        cur_start_row = active.start_row;
        if (active.clip_index >= 0) {
          bg = track_color(track_id);
          fg = styles.clip_text_color;
          // Only this instance's own leading bar shows its hex digit -
          // "leading" meaning the first bar row where it actually starts
          // resolving as active for this track (compared against
          // prev_clip_index/prev_start_row, not against active.start_row
          // itself: an instance placed off a bar boundary only starts
          // showing up here on whichever bar's own leading row is the
          // first at or past it, which is still its own leading bar for
          // this grid's purposes) - every later bar it's still active
          // through is blank, the color alone (via bg, and the padding
          // cells on either side) carrying the continuation.
          if (cur_clip_index != prev_clip_index[static_cast<size_t>(vc)] || cur_start_row != prev_start_row[static_cast<size_t>(vc)]) {
            glyph = fmt::format("{:x}", active.clip_index % 16);
          }
        } else if (active.clip_index == Arrangement::kStopInstance) {
          // An explicit stop was previously indistinguishable from plain
          // silence here - background left uncolored (there's nothing to
          // tint it with, unlike a real instance's own identity color).
          // The glyph itself is decided independently of `active` (which
          // only says playback is currently stopped here, inherited from
          // however many bars back the real stop event actually sits) -
          // barHasOwnStop() checks this bar's own row span directly, so
          // the marker is drawn purely because a stop is literally on this
          // line, not because a stop from several bars back still governs
          // it. Every real stop still shows, each on its own row, nothing
          // hidden - a later bar simply has nothing of its own to draw.
          fg = styles.window_fg_color;
          if (barHasOwnStop(arrangement, track_id, raw_row, rows_per_bar)) {
            // U+00D7 (multiplication sign), not the '*'/'.' glyphs above -
            // an ordinary narrow character, unlike the circle glyphs those
            // deliberately avoid, so no width-ambiguity risk of its own.
            glyph = "×";
          }
        } else {
          bool has_sounding_note = false;
          if (barHasBackgroundContent(arrangement, track_id, raw_row, rows_per_bar, has_sounding_note)) {
            // fg left at its plain default (styles.window_fg_color) - see
            // track_color()'s own comment on why this stays uncolored.
            // Plain ASCII, not a circle glyph (U+25CF/U+25CB) - those fall
            // in Unicode's "ambiguous width" class, rendered double-wide
            // by many terminal fonts, and this cell only ever has room
            // for one column.
            glyph = has_sounding_note ? "*" : ".";
          }
        }
      }

      prev_clip_index[static_cast<size_t>(vc)] = cur_clip_index;
      prev_start_row[static_cast<size_t>(vc)] = cur_start_row;

      if (is_playing_row) bg = styles.cursorRowTint(bg);

      // The shared/global track selection's own column - brightens an
      // active instance's own color (never the plain background, which
      // has no "selected" state of its own to show - no tint at all
      // there) so the selected column stays visible regardless of which
      // widget has focus, matching the one shared track cursor Launchpad
      // Live View already follows - not gated on `focused` for that
      // same reason, and not the full highlight below (that stays
      // reserved for this widget's own exact cursor cell).
      if (cur_clip_index >= 0 && track_index < num_tracks && track_ids[static_cast<size_t>(track_index)] == selected_track_id) {
        bg = bg.blend(0.35f, styles.cursor_tint_color);
      }

      // Unfocused, the cursor cell shows faintly - a colored instance cell
      // already shows it through the selected column's brightening above.
      // Focused, a colored cell brightens further rather than losing its
      // track color.
      auto is_cursor_cell = in_range && bar == cursor_bar_ && track_index == cursor_track_index_;
      if (is_cursor_cell && focused) {
        fg = styles.highlight_fg_color;
        bg = cur_clip_index >= 0 ? bg.blend(0.5f, styles.cursor_tint_color) : styles.highlight_bg_color;
      } else if (is_cursor_cell && cur_clip_index < 0) {
        bg = styles.highlight_unfocused_bg_color;
      }

      setFgColor(prev_bg);
      setBgColor(bg);
      putstr(vr, pad_x, "▌");

      setFgColor(fg);
      setBgColor(bg);
      putstr(vr, digit_x, glyph);

      prev_bg = bg;
    }

    // The trailing padding cell after the last visible track, symmetric
    // with prev_bg's own initial value above - nothing past this column
    // either.
    if (visible_cols > 0) {
      setFgColor(prev_bg);
      setBgColor(styles.window_bg_color);
      putstr(vr, visible_cols * kColWidth, "▌");
    }

    // A locator anywhere in this bar - which row isn't shown. The playing
    // bar brightens the mark itself rather than tinting its background.
    auto locator = locators.lower_bound(raw_row);
    bool has_locator = in_range && locator != locators.end() && locator->first < raw_row + rows_per_bar;
    setFgColor(is_playing_row ? styles.cursor_tint_color : styles.locator_mark_color);
    setBgColor(styles.window_bg_color);
    putstr(vr, visible_cols * kColWidth + kLocatorWidth, has_locator ? "›" : " ");
  }

  return true;
}
