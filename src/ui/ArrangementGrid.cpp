#include "ArrangementGrid.h"

#include "../playback/InputEvent.h"
#include "StyleProvider.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/Scene.h"
#include "../model/Clip.h"
#include "../model/ArrangementOps.h"
#include "../model/SongStructure.h"
#include "../util/Utf8.h"
#include "KeyChord.h"

#include <algorithm>
#include <fmt/core.h>

using namespace std;

vector<int>
ArrangementGrid::getVisibleTrackIds(const Song & song) const {
  return song.getPlayableTrackIds();
}

int
ArrangementGrid::barsPerScene(const Song & song) const {
  auto rows_per_bar = max(1, song.getRowsPerBar());
  return max(1, song.getPatternLength() / rows_per_bar);
}

void
ArrangementGrid::moveCursorScene(const Song & song, int delta) {
  auto num_scenes = static_cast<int>(song.getScenes().size());
  cursor_scene_ = clamp(cursor_scene_ + delta, 0, num_scenes);
  cursor_bar_ = 0;
}

void
ArrangementGrid::moveCursorRow(const Song & song, int delta) {
  auto bars_per_scene = barsPerScene(song);
  auto slot_size = 1 + bars_per_scene; // a title row plus its own bar rows
  auto num_scenes = static_cast<int>(song.getScenes().size());
  auto flat = cursor_scene_ * slot_size + (cursor_bar_ + 1) + delta;
  auto max_flat = num_scenes * slot_size; // the virtual "one past the end" scene's own title row
  flat = clamp(flat, 0, max_flat);
  cursor_scene_ = flat / slot_size;
  cursor_bar_ = flat % slot_size - 1;
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
ArrangementGrid::ensureCursorVisible(const Song & song, int visible_rows, int visible_cols, int num_tracks, bool follow_cursor) {
  auto bars_per_scene = barsPerScene(song);
  auto slot_size = 1 + bars_per_scene; // a title row plus its own bar rows
  auto num_scenes = static_cast<int>(song.getScenes().size());
  // +1 scene worth of rows: the cursor (and the scrolled viewport) may
  // reach exactly the virtual "one past the end" scene's own title row -
  // a not-yet-instantiated scene (see offerInput()'s own NCKEY_ENTER/
  // NCKEY_DOWN comments; this widget has no clipboard of its own -
  // committing there is what creates it).
  auto row_slots = num_scenes * slot_size + 1;
  cursor_scene_ = clamp(cursor_scene_, 0, num_scenes);
  cursor_bar_ = clamp(cursor_bar_, -1, bars_per_scene - 1);
  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));

  // Always kept in bounds (a shrunk song must never leave a stale
  // out-of-range scroll position behind), regardless of follow_cursor.
  scroll_row_ = clamp(scroll_row_, 0, max(0, row_slots - visible_rows));
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));

  // Not focused - render() keeps the playhead in view instead (see its
  // own comment on why only one of the two ever drives the scroll
  // position in a given frame).
  if (!follow_cursor) return;

  auto cursor_flat = cursor_scene_ * slot_size + (cursor_bar_ + 1);
  if (cursor_flat < scroll_row_) scroll_row_ = cursor_flat;
  if (visible_rows > 0 && cursor_flat >= scroll_row_ + visible_rows) scroll_row_ = cursor_flat - visible_rows + 1;
  scroll_row_ = clamp(scroll_row_, 0, max(0, row_slots - visible_rows));

  if (cursor_track_index_ < scroll_col_) scroll_col_ = cursor_track_index_;
  if (visible_cols > 0 && cursor_track_index_ >= scroll_col_ + visible_cols) scroll_col_ = cursor_track_index_ - visible_cols + 1;
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));
}

bool
ArrangementGrid::offerInput(const InputEvent & input) {
  // Mirrors PatternEditor::offerInput()'s own reader-active handling:
  // while the scene-name editor (startSceneRename()) is open, Enter
  // commits and Ctrl-g cancels; everything else goes to the reader
  // instead of any of this class's own keybinding dispatch/manual
  // handling below.
  if (getPlane().readerActive()) {
    if (input.getId() == NCKEY_ENTER) {
      auto text = getPlane().closeReader();
      if (renaming_scene_idx_ >= 0) {
        auto & song = getController().getSong();
        auto & scene = song.getOrCreateScene(renaming_scene_idx_);
        scene.setName(std::move(text));
        song.incVersion();
      }
      renaming_scene_idx_ = -1;
      force_redraw_ = true;
      return true;
    } else if (input.hasCtrl() && input.getId() == 'g') {
      getPlane().closeReader();
      renaming_scene_idx_ = -1;
      force_redraw_ = true;
      return true;
    } else {
      return getPlane().offerInput(input);
    }
  }

  if (dispatchCommand(input)) return true;
  if (input.getKind() == InputEvent::Kind::RELEASE) return false;

  auto & song = getController().getSong();
  auto track_ids = getVisibleTrackIds(song);
  auto num_tracks = static_cast<int>(track_ids.size());
  auto rows_per_bar = max(1, song.getRowsPerBar());

  if (input.getId() == NCKEY_ENTER) {
    // On the title row, there's no (track, row) to commit - open the
    // scene's own name for editing instead.
    if (cursor_bar_ < 0) {
      startSceneRename();
      return true;
    }
    // cursor_scene_ pointing at the one-past-the-end virtual scene is a
    // real, valid target here, not refused: it just moves the playhead/
    // track selection there, same as PatternEditor's own row navigation
    // already tolerates running off the end of the last real Scene.
    // Deliberately NOT instantiating a Scene on commit - keeping "look
    // at/jump to this position" and "put real content here" as two
    // separate actions.
    if (commit_callback_ && cursor_track_index_ < num_tracks) {
      commit_callback_(track_ids[static_cast<size_t>(cursor_track_index_)], cursor_scene_, cursor_bar_ * rows_per_bar);
    }
    return true;
  }

  if (input.getId() == NCKEY_UP) moveCursorRow(song, -1);
  else if (input.getId() == NCKEY_DOWN) moveCursorRow(song, 1);
  // A full screenful at a time - same "jump by the viewport's own
  // height" reading Page Up/Down carry everywhere else in this app,
  // rather than PatternEditor's own fixed 16-row jump (which counts raw
  // pattern rows, not this grid's own bar/title rows, so a fixed count
  // wouldn't mean the same thing here).
  else if (input.getId() == NCKEY_PGUP) moveCursorRow(song, -getDim().first);
  else if (input.getId() == NCKEY_PGDOWN) moveCursorRow(song, getDim().first);
  else if (input.getId() == NCKEY_BACKSPACE) {
    // Meaningless on the title row - there's no (track, row) there to
    // place a stop instance at.
    if (cursor_bar_ < 0) return false;
    if (cursor_track_index_ < num_tracks) {
      auto & scene = song.getOrCreateScene(cursor_scene_);
      placeStopInstance(scene, track_ids[static_cast<size_t>(cursor_track_index_)], cursor_bar_ * rows_per_bar);
      song.incVersion();
    }
  }
  // Left/Right move across per-track columns, which only exist on a bar
  // row - meaningless on the title row (cursor_bar_ < 0), so left alone
  // for Up/Down to handle instead of moving a column index nothing is
  // showing right now, or (Right) firing exit_right_callback_ on a row
  // that was never actually at the rightmost real column to begin with.
  else if (cursor_bar_ < 0) return false;
  else if (input.getId() == NCKEY_LEFT) cursor_track_index_--;
  else if (input.getId() == NCKEY_RIGHT) {
    // Already on the last (rightmost) track column - nowhere further
    // right to go in the overview itself, so this leaves it and hands
    // focus back to PatternEditor instead of a no-op clamp.
    if (cursor_track_index_ >= num_tracks - 1 && exit_right_callback_) {
      exit_right_callback_();
      return true;
    }
    cursor_track_index_++;
  }
  else return false;

  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));
  return true;
}

void
ArrangementGrid::startSceneRename() {
  if (getPlane().readerActive()) return;

  auto & song = getController().getSong();
  auto slot_size = 1 + barsPerScene(song);
  auto row = cursor_scene_ * slot_size - scroll_row_;
  auto [ rows, cols ] = getDim();
  if (row < 0 || row >= rows) return; // off-screen - shouldn't happen given ensureCursorVisible(), a cosmetic nuisance if it ever does

  renaming_scene_idx_ = cursor_scene_;

  // Blanks the target row first - the reader plane's own base cell
  // (TerminalUI::showReader()'s ncplane_set_base(..., "", ...)) doesn't
  // paint over cells nothing ever explicitly writes to, so without this a
  // shorter new name than the old one would leave stale characters
  // peeking out past the reader's own text.
  setFgColor(0, 0, 0);
  setBgColor(0, 0, 0);
  putstr(row, 0, string(static_cast<size_t>(cols), ' '));

  getPlane().showReader("", row, 0, 1, cols, song.getScene(cursor_scene_).getName());
}

namespace {

// Whether `track_id`'s own background Pattern has anything defined
// across [raw_row, raw_row + rows_per_bar) - the fallback shown for a
// bar with no active clip instance. `has_sounding_note` tells a real
// note-on apart from a bar that's non-empty purely from note-offs/
// aftertouch/Command data (Pattern::hasSoundingNote()'s own distinction,
// applied per-bar instead of to a whole Pattern).
bool barHasBackgroundContent(const Scene & scene, int track_id, int raw_row, int rows_per_bar, int context_length, bool & has_sounding_note) {
  bool has_any = false;
  has_sounding_note = false;
  for (int row = raw_row; row < raw_row + rows_per_bar; row++) {
    auto effective_row = scene.getEffectiveRow(track_id, row, context_length);
    for (auto & note : scene.getNotes(effective_row, track_id)) {
      if (note.isDefined()) {
        has_any = true;
        if (!note.isOff() && !note.isAftertouch()) has_sounding_note = true;
      }
    }
    if (scene.getCommand(effective_row, track_id).isDefined()) has_any = true;
  }
  return has_any;
}

}

bool
ArrangementGrid::render(const StyleProvider & styles, bool refresh, bool focused, int selected_track_id) {
  auto & song = getController().getSong();
  auto track_ids = getVisibleTrackIds(song);
  auto num_scenes = static_cast<int>(song.getScenes().size());
  auto num_tracks = static_cast<int>(track_ids.size());
  auto rows_per_bar = max(1, song.getRowsPerBar());
  auto bars_per_scene = barsPerScene(song);
  auto slot_size = 1 + bars_per_scene; // a title row plus its own bar rows

  auto [ rows, cols ] = getDim();
  if (rows < 1 || cols < 1) return false;
  // 2 columns per track - the identifier cell itself, plus one shared
  // half-width padding cell (see the render loop's own draw_edge below)
  // - and one more standalone padding cell at the very start, for the
  // first track's own left edge.
  constexpr int kColWidth = 2;
  auto visible_rows = rows;
  auto visible_cols = max(0, cols - 1) / kColWidth;

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
  // transport position instead - getPatternIndex()/getRowIndex() is the
  // edit position while stopped, matching PatternEditor's own
  // stopped-row highlight, which isn't gated on isPlaying() either.
  auto & playback_info = getController().getPlaybackInfo();
  auto follow_cursor = !playback_info.isPlaying() && focused;
  ensureCursorVisible(song, visible_rows, visible_cols, num_tracks, follow_cursor);

  auto playing_scene = playback_info.getPatternIndex();
  auto playing_row = playback_info.getRowIndex();

  if (!follow_cursor && playing_scene >= 0 && playing_scene < num_scenes) {
    auto playhead_flat = playing_scene * slot_size + 1 + playing_row / rows_per_bar;
    if (playhead_flat < scroll_row_) scroll_row_ = playhead_flat;
    if (visible_rows > 0 && playhead_flat >= scroll_row_ + visible_rows) scroll_row_ = playhead_flat - visible_rows + 1;
    auto row_slots = num_scenes * slot_size + 1;
    scroll_row_ = clamp(scroll_row_, 0, max(0, row_slots - visible_rows));
  }

  auto new_version = song.getMajorVersion();

  if (!refresh && !force_redraw_ && new_version == current_song_version_ && playing_scene == current_playing_scene_ &&
      playing_row == current_playing_row_ &&
      cursor_scene_ == current_cursor_scene_ && cursor_bar_ == current_cursor_bar_ &&
      cursor_track_index_ == current_cursor_track_index_ &&
      scroll_row_ == current_scroll_row_ && scroll_col_ == current_scroll_col_ &&
      focused == current_focused_ && selected_track_id == current_selected_track_id_) {
    return false;
  }
  force_redraw_ = false;
  current_song_version_ = new_version;
  current_playing_scene_ = playing_scene;
  current_playing_row_ = playing_row;
  current_cursor_scene_ = cursor_scene_;
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
  const Color kWhite(255, 255, 255);

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
  // Reset to "nothing active" at every title row (see below) - an
  // instance never carries across a scene boundary, so two different
  // scenes each starting a placement at the same clip_index/start_row
  // must never look like one continuing across them.
  vector<int> prev_clip_index(static_cast<size_t>(visible_cols), Scene::kNoInstance);
  vector<int> prev_start_row(static_cast<size_t>(visible_cols), -1);

  for (auto vr = 0; vr < visible_rows; vr++) {
    auto flat_row = scroll_row_ + vr;
    auto scene_idx = flat_row / slot_size;
    auto local = flat_row % slot_size;
    auto is_title_row = local == 0;
    auto bar_in_scene = local - 1;
    // Past the virtual "one past the end" scene - or, within it, past its
    // own single valid title row (nothing exists there yet to have any
    // bars) - nothing left to show. Still painted explicitly below rather
    // than left to whatever an earlier frame drew in this same screen
    // cell: erase()'s own fallback background doesn't reach a cell that
    // never gets its own putstr() call.
    auto in_range = scene_idx < num_scenes || (scene_idx == num_scenes && is_title_row);
    auto & scene = song.getScene(scene_idx);

    if (is_title_row) {
      auto is_cursor_row = focused && in_range && scene_idx == cursor_scene_ && cursor_bar_ < 0;
      // Plain window_bg_color, same as every other cell in this grid -
      // only the accent foreground (below) sets a title row apart from
      // an ordinary blank one.
      Color fg = styles.window_fg_color, bg = styles.window_bg_color;
      if (in_range) fg = styles.window_accent_fg_color;
      if (is_cursor_row) {
        fg = styles.highlight_fg_color;
        bg = styles.highlight_bg_color;
      }
      setFgColor(fg);
      setBgColor(bg);
      // The virtual scene has no name of its own to show yet (nothing's
      // been placed there - see startSceneRename()'s own comment on what
      // renaming it does); a real scene falls back to a placeholder
      // rather than a blank row, so an unnamed scene still reads as "a
      // scene is here" rather than looking like empty space.
      string name;
      if (in_range && scene_idx < num_scenes) name = scene.getName().empty() ? "(untitled)" : scene.getName();
      name = Utf8::truncateToWidth(name, cols);
      name = Utf8::padToWidth(name, cols);
      putstr(vr, 0, name);
      std::fill(prev_clip_index.begin(), prev_clip_index.end(), Scene::kNoInstance);
      std::fill(prev_start_row.begin(), prev_start_row.end(), -1);
      continue;
    }

    auto raw_row = bar_in_scene * rows_per_bar;
    auto is_playing_row = in_range && scene_idx == playing_scene && bar_in_scene == playing_row / rows_per_bar;

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
      int cur_clip_index = Scene::kNoInstance, cur_start_row = -1;

      if (in_range && track_index < num_tracks) {
        auto track_id = track_ids[static_cast<size_t>(track_index)];
        auto active = resolveInstanceAt(song, scene, track_id, raw_row);
        cur_clip_index = active.clip_index;
        cur_start_row = active.start_row;
        if (active.clip_index >= 0) {
          bg = track_color(track_id);
          fg = kWhite;
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
        } else {
          bool has_sounding_note = false;
          if (barHasBackgroundContent(scene, track_id, raw_row, rows_per_bar, song.getPatternLength(), has_sounding_note)) {
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

      if (is_playing_row) bg = bg.blend(0.15f, kWhite);

      // The shared/global track selection's own column, every row - a
      // light tint (not the full highlight below, which stays reserved
      // for this widget's own exact cursor cell) so the selected column
      // stays visible regardless of which widget has focus, matching the
      // one shared track cursor Launchpad Session view already follows -
      // not gated on `focused` for that same reason.
      if (track_index < num_tracks && track_ids[static_cast<size_t>(track_index)] == selected_track_id) {
        bg = bg.blend(0.25f, styles.highlight_bg_color);
      }

      // Distracting otherwise, and ambiguous about which window Enter
      // would actually commit - see PatternEditor's own equivalent
      // gating for the same reasoning.
      auto is_cursor_cell = focused && in_range && scene_idx == cursor_scene_ && bar_in_scene == cursor_bar_ && track_index == cursor_track_index_;
      if (is_cursor_cell) {
        fg = styles.highlight_fg_color;
        bg = styles.highlight_bg_color;
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
  }

  return true;
}
