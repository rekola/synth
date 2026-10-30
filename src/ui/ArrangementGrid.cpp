#include "ArrangementGrid.h"

#include "../playback/InputEvent.h"
#include "StyleProvider.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/Arrangement.h"
#include "../model/Clip.h"
#include "../model/ArrangementOps.h"
#include "../model/SongStructure.h"
#include "KeyStroke.h"

#include <algorithm>
#include <cellui/Utf8.h>
#include <fmt/format.h>

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

ArrangementGrid::ArrangementGrid(Plane & parent) : UIElement(parent) {
  getPlane().setName("arrangement-grid");
  getPlane().setMouseCursor(cellui::MouseCursor::POINTER);
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

  connections_.push_back(on<cellui::MouseDown>([this](const cellui::MouseDown & e, cellui::EventContext &) {
    return e.button == 1 && pick(e.row, e.col) ? cellui::Handled::yes : cellui::Handled::no;
  }));
  // The release ends the click; nothing else to do with it.
  connections_.push_back(on<cellui::MouseUp>([](const cellui::MouseUp & e, cellui::EventContext &) {
    return e.button == 1 ? cellui::Handled::yes : cellui::Handled::no;
  }));
  // The wheel scrolls the view along the bars (Shift: tracks), not the
  // cursor, and detaches it from the cursor/playhead until the cursor next moves.
  connections_.push_back(on<cellui::Wheel>([this](const cellui::Wheel & e, cellui::EventContext &) {
    bool sideways = e.dx != 0;
    int direction = sideways ? (e.dx > 0 ? 1 : -1) : (e.dy > 0 ? -1 : 1);
    bool along_bars = sideways || !e.mods.shift;
    viewport_.wheel(direction, along_bars, along_bars ? wheel_acceleration_.step(kWheelBars) : 1);
    return cellui::Handled::yes;
  }));
}

// A left click picks the (bar, track) cell under it; Enter still commits it.
bool ArrangementGrid::pick(int screen_row, int screen_col) {
  auto & song = getController().getSong();
  auto num_tracks = static_cast<int>(getVisibleTrackIds(song).size());
  auto [pos_y, pos_x] = getPosition();
  auto [rows, cols] = getDim();
  auto y = screen_row - pos_y, x = screen_col - pos_x;
  if (y < 0 || y >= rows || x < 0 || x >= cols) return false;
  // Left of the bars is the track names, above the first track the
  // bar ruler: each picks only its own axis.
  if (y < kHeaderRows && x < kNameWidth) return true;
  auto playing_bar = song.getArrangementBars().barIndex(getController().getPlaybackInfo().getAbsolutePosition());
  if (y >= kHeaderRows) {
    auto track = viewport_.rows.offset() + y - kHeaderRows;
    if (track >= num_tracks) return true; // below the last track
    cursor_track_index_ = track;
  }
  if (x >= kNameWidth) {
    auto bar = viewport_.cols.indexAt(x - kNameWidth);
    if (bar >= barCount(song, playing_bar)) return true; // past the last bar
    cursor_bar_ = bar;
  }
  viewport_.attach();
  return true;
}

void
ArrangementGrid::ensureCursorVisible(int bar_count, int visible_rows, int visible_cols, int num_tracks, bool follow_cursor) {
  cursor_bar_ = clamp(cursor_bar_, 0, bar_count - 1);
  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));

  // Always kept in bounds (a shrunk song must never leave a stale
  // out-of-range scroll position behind), regardless of follow_cursor.
  viewport_.setExtent(num_tracks, bar_count, visible_rows, visible_cols);

  // Not focused - render() keeps the playhead in view instead (see its
  // own comment on why only one of the two ever drives the scroll
  // position in a given frame).
  if (!follow_cursor) return;

  viewport_.rows.reveal(cursor_track_index_);
  viewport_.cols.reveal(cursor_bar_);
}

bool ArrangementGrid::offerInput(const cellui::Input & input) {
  if (dispatchCommand(input)) return true;
  if (input.getKind() == cellui::Input::Kind::RELEASE) return false;

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

  if (input.getId() == Key::Enter) {
    // A bar past the arrangement's end is a valid target: it just moves
    // the playhead/track selection there - "look at/jump to this
    // position" and "put real content here" stay two separate actions.
    if (commit_callback_ && cursor_track_index_ < num_tracks) {
      commit_callback_(track_ids[static_cast<size_t>(cursor_track_index_)], bar_row(cursor_bar_));
    }
    return true;
  }

  // Time runs to the right: Left/Right move along the bars, Up/Down across
  // the tracks, stopping at the grid's own edges.
  if (input.getId() == Key::Left)
    move_cursor(-1);
  else if (input.getId() == Key::Right)
    move_cursor(1);
  // A full screenful of bars at a time - same "jump by the viewport's own
  // width" reading Page Up/Down carry everywhere else in this app, rather
  // than PatternEditor's own fixed 16-row jump (which counts raw pattern
  // rows, not this grid's own bars, so a fixed count wouldn't mean the same
  // thing here).
  else if (input.getId() == Key::PageUp)
    move_cursor(-max(1, getDim().second - kNameWidth));
  else if (input.getId() == Key::PageDown)
    move_cursor(max(1, getDim().second - kNameWidth));
  else if (input.getId() == Key::Backspace) {
    if (cursor_track_index_ < num_tracks) {
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
  else if (input.getId() == Key::Delete || (input.hasCtrl() && input.getId() == 'k')) {
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
        getController().log(std::move(text));
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
        getController().log("Deleted stop");
      }
    }
  } else if (input.getId() == Key::Up)
    cursor_track_index_--;
  else if (input.getId() == Key::Down)
    cursor_track_index_++;
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
  if (rows <= kHeaderRows || cols <= kNameWidth) return false;
  auto visible_tracks = rows - kHeaderRows;
  auto visible_cols = cols - kNameWidth;
  auto locators = song.getLocators();

  // The bars are a plane of their own, right of the names, so that only they
  // (and the ruler over them) shift by parts of a bar.
  auto [pos_y, pos_x] = getPosition();
  if (!bars_plane_) {
    bars_plane_ = getPlane().createChild();
    bars_plane_->setMouseCursor(cellui::MouseCursor::POINTER);
  }
  if (std::make_tuple(pos_y, pos_x, rows, cols) != laid_out_) {
    laid_out_ = std::make_tuple(pos_y, pos_x, rows, cols);
    bars_plane_->move(pos_y, pos_x + kNameWidth);
    bars_plane_->resize(rows, visible_cols);
    refresh = true;
  }
  bars_plane_->setVisible(shown_);
  viewport_.rows.setSmooth(false);
  viewport_.cols.setSmooth(getPlane().smoothScroll());

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
  if (viewport_.detached() && (edit_position_moved || cursor_bar_ != current_cursor_bar_ ||
                               cursor_track_index_ != current_cursor_track_index_)) viewport_.attach();
  auto follow_cursor = !playback_info.isPlaying() && focused && !viewport_.detached();
  // How focused the widget is, eased: its cursor fades between the focused and unfocused look.
  focus_ = fades_.value(0, focused ? 1.0f : 0.0f, !styles.reduced_motion);
  // One more frame after it ends draws the final state.
  bool focus_fading = fades_.active() || focus_fade_drawn_;
  focus_fade_drawn_ = fades_.active();
  fades_.sweep();
  ensureCursorVisible(bar_count, visible_tracks, visible_cols, num_tracks, follow_cursor);

  if (!follow_cursor && !viewport_.detached()) viewport_.cols.reveal(playing_bar);

  // Where the transport is inside its bar, while it plays (0 to 255).
  auto phase = playback_info.isPlaying() ? playback_info.getCurrentDelay() : 0;
  // On a display that scrolls smoothly the playhead is a line of its own
  // that moves through the bars, not a bar's tint that steps.
  bool smooth_playhead = viewport_.cols.smooth() && playback_info.isPlaying();
  auto new_version = song.getMajorVersion();
  {
    // The view follows where it was scrolled to.
    auto now = std::chrono::steady_clock::now();
    viewport_.tick(std::min(0.1, std::chrono::duration<double>(now - last_tick_).count()));
    last_tick_ = now;
  }

  if (!refresh && new_version == current_song_version_ && playing_row == current_playing_row_ && smooth_playhead == current_smooth_playhead_ && (!smooth_playhead || phase == current_phase_) &&
      cursor_bar_ == current_cursor_bar_ &&
      cursor_track_index_ == current_cursor_track_index_ &&
      viewport_.cols.shown() == current_shown_col_ && viewport_.rows.offset() == current_scroll_row_ &&
      focused == current_focused_ && !focus_fading && selected_track_id == current_selected_track_id_ &&
      shown_ == current_shown_) {
    return false;
  }
  current_song_version_ = new_version;
  current_playing_row_ = playing_row;
  current_phase_ = phase;
  current_smooth_playhead_ = smooth_playhead;
  current_cursor_bar_ = cursor_bar_;
  current_cursor_track_index_ = cursor_track_index_;
  current_shown_col_ = viewport_.cols.shown();
  current_scroll_row_ = viewport_.rows.offset();
  current_selected_track_id_ = selected_track_id;
  current_focused_ = focused;
  current_shown_ = shown_;

  Plane & bars_plane = *bars_plane_;
  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  erase();
  bars_plane.setFgColor(styles.window_fg_color);
  bars_plane.setBgColor(styles.window_bg_color);
  bars_plane.erase();
  // The bars are drawn from the first one the view shows, with one more than
  // fit, and the plane shifts them by the part of a bar the view is between.
  auto first_bar = viewport_.cols.firstShown();
  auto drawn_cols = visible_cols + (viewport_.cols.smooth() ? 1 : 0);
  bars_plane.setScrollOffset(0, viewport_.cols.fraction());

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
    return structure.getBaselineInfo(track_id).getColor(styles.tracks);
  };

  // The names, as the outline shows them.
  std::unordered_map<int, string> labels;
  for (auto & track : song.getMasterTrack().getChildren()) {
    auto & baseline = structure.getBaselineInfo(track->getInternalId());
    if (baseline.color_ordinal_ < 0) continue;
    auto label = "T" + to_string(baseline.color_ordinal_);
    if (!track->getName().empty()) label += " " + track->getName();
    labels[track->getInternalId()] = label;
  }

  // The header: the ruler (a bar number every fourth bar) and the locators,
  // each followed by its name. Its panel colour keeps it apart from the menu
  // bar above.
  const auto header_bg = styles.window_accent_bg_color;
  setBgColor(header_bg);
  for (int y = 0; y < kHeaderRows; y++) putstr(y, 0, string(static_cast<size_t>(kNameWidth), ' '));
  bars_plane.setBgColor(header_bg);
  for (int y = 0; y < kHeaderRows; y++) bars_plane.putstr(y, 0, string(static_cast<size_t>(drawn_cols), ' '));
  // The ruler and the locator row's background, the playing bar's tinted.
  for (auto vc = 0; vc < drawn_cols; vc++) {
    auto bar = first_bar + vc;
    if (bar >= bar_count) break;
    auto bg = header_bg;
    if (bar == playing_bar && !smooth_playhead) bg = styles.cursorRowTint(bg);
    // The label of the fourth bar at or before this one, read at this offset.
    auto label_bar = bar - bar % 4;
    auto label = to_string(label_bar + 1);
    auto offset = static_cast<size_t>(bar - label_bar);
    bars_plane.setBgColor(bg);
    bars_plane.setFgColor(bar == playing_bar ? styles.cursor_tint_color : styles.window_fg_color);
    bars_plane.putstr(0, vc, label.size() <= 3 && offset < label.size() ? label.substr(offset, 1) : " ");
    bars_plane.putstr(1, vc, " ");
  }
  // Each locator is its mark and name written as a string from its bar, as
  // many columns as are free before the next locator or the edge: the plane
  // places wide characters, so nothing here counts characters as columns. A
  // locator that begins left of the view drops the columns that are out of it.
  int playing_col = smooth_playhead ? -1 : playing_bar - first_bar;
  vector<pair<int, string>> locator_texts;
  for (auto & [locator_row, name] : locators) {
    auto locator_bar = bars.barIndex(locator_row);
    if (locator_bar >= first_bar + drawn_cols) break;
    locator_texts.emplace_back(locator_bar - first_bar, "›" + name);
  }
  for (size_t i = 0; i < locator_texts.size(); i++) {
    auto [start, text] = locator_texts[i];
    auto end = i + 1 < locator_texts.size() ? locator_texts[i + 1].first : drawn_cols;
    if (start < 0) {
      text = cellui::utf8::dropLeadingColumns(text, -start);
      start = 0;
    }
    if (end <= start) continue;
    text = cellui::utf8::truncateToWidth(text, end - start);
    bars_plane.setBgColor(header_bg);
    bars_plane.setFgColor(styles.locator_mark_color);
    bars_plane.putstr(1, start, text);
    // The playing bar's cell inside the text is written again on its tint.
    if (playing_col >= start && playing_col < end) {
      int col = start;
      for (auto & cluster : cellui::utf8::clusters(text)) {
        auto width = max(1, cellui::utf8::displayWidth(cluster));
        if (playing_col < col + width) {
          bars_plane.setBgColor(styles.cursorRowTint(header_bg));
          bars_plane.setFgColor(styles.locator_mark_color);
          bars_plane.putstr(1, col, cluster);
          break;
        }
        col += width;
      }
    }
  }

  auto arrangement = song.getArrangement();

  for (auto vt = 0; vt < visible_tracks; vt++) {
    auto track_index = viewport_.rows.offset() + vt;
    if (track_index >= num_tracks) break;
    auto track_id = track_ids[static_cast<size_t>(track_index)];
    auto y = kHeaderRows + vt;
    // Shown only while this grid has the focus.
    bool selected = focused && track_id == selected_track_id;
    auto color = track_color(track_id);

    // The name, after a swatch in the track's color; the selected track's is
    // marked like a row of the pattern editor.
    auto name_bg = selected ? styles.cursorRowTint(styles.window_bg_color) : styles.window_bg_color;
    setFgColor(color);
    setBgColor(name_bg);
    putstr(y, 0, "▌");
    setFgColor(styles.window_fg_color);
    putstr(y, 1, cellui::utf8::padToWidth(cellui::utf8::truncateToWidth(labels[track_id], kNameWidth - 2), kNameWidth - 1));

    // The active instance the previous bar showed for this track - lets the
    // loop below tell "still the same instance, one bar further into it"
    // apart from "a different instance just became active here" without
    // needing to assume an instance's own start_row lands on a bar boundary
    // at all (an instance placed off-grid, e.g. via hand-edited XML, only
    // starts actually resolving on whichever bar's own leading row is the
    // first at or past it - that bar is still its own leading bar for this
    // grid's purposes). Seeded from the bar left of the view, so an
    // instance continuing into it doesn't show its digit again.
    int prev_clip_index = Arrangement::kNoInstance, prev_start_row = -1;
    if (first_bar > 0) {
      auto left_row = bars.barStartRow(first_bar - 1);
      auto left = resolveInstanceForBar(song, track_id, left_row, bars.nextBarStart(left_row) - left_row);
      prev_clip_index = left.clip_index;
      prev_start_row = left.start_row;
    }

    for (auto vc = 0; vc < drawn_cols; vc++) {
      auto bar = first_bar + vc;
      // Past the bars there are, nothing left to show.
      if (bar >= bar_count) break;
      auto raw_row = bars.barStartRow(bar);
      auto rows_per_bar = bars.nextBarStart(raw_row) - raw_row; // this bar's own length

      string glyph = " ";
      Color fg = styles.window_fg_color, bg = styles.window_bg_color;
      // Bar-granularity, not resolveInstanceAt(raw_row) directly - a short
      // one-shot instance that both starts and finishes again entirely
      // inside this one bar's own row span would otherwise never touch any
      // bar's plain per-row sample at all (see resolveInstanceForBar()'s
      // own comment).
      auto active = resolveInstanceForBar(song, track_id, raw_row, rows_per_bar);
      if (active.clip_index >= 0) {
        bg = color;
        fg = styles.clip_text_color;
        // Only this instance's own leading bar shows its hex digit -
        // every later bar it's still active through is blank, the color
        // alone carrying the continuation.
        if (active.clip_index != prev_clip_index || active.start_row != prev_start_row) {
          glyph = fmt::format("{:x}", active.clip_index % 16);
        }
      } else if (active.clip_index == Arrangement::kStopInstance) {
        // `active` only says playback is currently stopped here, inherited
        // from however many bars back the real stop event sits - the marker
        // is drawn purely because a stop is literally on this bar.
        if (barHasOwnStop(arrangement, track_id, raw_row, rows_per_bar)) {
          // U+00D7 (multiplication sign): an ordinary narrow character, so
          // no width-ambiguity risk of its own.
          glyph = "×";
        }
      } else {
        bool has_sounding_note = false;
        if (barHasBackgroundContent(arrangement, track_id, raw_row, rows_per_bar, has_sounding_note)) {
          // Plain ASCII, not a circle glyph (U+25CF/U+25CB) - those fall in
          // Unicode's "ambiguous width" class, rendered double-wide by many
          // terminal fonts, and this cell only ever has room for one column.
          glyph = has_sounding_note ? "*" : ".";
        }
      }
      prev_clip_index = active.clip_index;
      prev_start_row = active.start_row;

      if (bar == playing_bar && !smooth_playhead) bg = styles.cursorRowTint(bg);

      // The shared track selection's own row - brightens an active
      // instance's own color (never the plain background, which has no
      // "selected" state of its own to show) so the selected track stays
      // visible regardless of which widget has focus, matching the one
      // shared track cursor Launchpad Live View also follows - not gated
      // on `focused` for that same reason, and not the full highlight below
      // (that stays reserved for this widget's own exact cursor cell).
      if (active.clip_index >= 0 && selected) bg = bg.blend(0.35f, styles.cursor_tint_color);

      // The cursor shows only while the window has the focus.
      // Unfocused, the cursor cell shows faintly - a colored instance cell
      // already shows it through the selected row's brightening above.
      // Focused, a colored cell brightens further rather than losing its
      // track color.
      if (styles.window_focused && bar == cursor_bar_ && track_index == cursor_track_index_) {
        if (active.clip_index >= 0) {
          if (focus_ > 0.0f) {
            fg = fg.blend(focus_, styles.highlight_fg_color);
            bg = bg.blend(0.5f * focus_, styles.cursor_tint_color);
          }
        } else {
          bg = styles.highlight_unfocused_bg_color.blend(focus_, styles.highlight_bg_color);
          fg = fg.blend(focus_, styles.highlight_fg_color);
        }
      }

      bars_plane.setFgColor(fg);
      bars_plane.setBgColor(bg);
      bars_plane.putstr(y, vc, glyph);
    }
  }

  if (smooth_playhead) {
    // The playhead: a line through the playing bar, as far through it as the
    // transport is, over the bars as they scroll.
    auto bar_start = bars.barStartRow(playing_bar);
    auto bar_rows = std::max(1, bars.nextBarStart(bar_start) - bar_start);
    double bar_position = playing_bar + (playing_row - bar_start + phase / 256.0) / bar_rows;
    constexpr double kLineCols = 0.25;
    bars_plane.setBand(cellui::Band{0.0, static_cast<double>(rows), bar_position - viewport_.cols.shown() - kLineCols / 2, kLineCols, styles.cursor_tint_color, 1.0f,
                                    cellui::BlendMode::NORMAL, styles.theme.elevation.card});
  } else {
    bars_plane.clearBand();
  }

  return true;
}
