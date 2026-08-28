#include "PatternMatrix.h"

#include "../playback/InputEvent.h"
#include "../playback/LogEvent.h"
#include "StyleProvider.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/SongStructure.h"
#include "KeyChord.h"

#include <algorithm>
#include <memory>
#include <string>

using namespace std;

vector<int>
PatternMatrix::getVisibleTrackIds(const Song & song) const {
  return song.getPlayableTrackIds();
}

void
PatternMatrix::moveCursorScene(const Song & song, int delta) {
  // Same clamp offerInput()'s own NCKEY_UP/NCKEY_DOWN handling uses - one
  // position past the last real Scene is still a valid target (see
  // ensureCursorVisible()'s own comment).
  auto num_scenes = static_cast<int>(song.getScenes().size());
  cursor_scene_ = std::clamp(cursor_scene_ + delta, 0, num_scenes);
}

PatternMatrix::PatternMatrix(UIPlane & parent) : UIElement(parent) {
  // Same command names, same chords, as PatternEditor's own kill-region/
  // kill-ring-save/yank - see this class's own header comment on why: the
  // active element's own registry is checked first (UI::executeCommand()),
  // so the same C-w/M-w/C-y keystroke reaches whichever of the two is
  // focused. Defining all three here (even kill-region, whose body is
  // barely more than kill-ring-save's) matters beyond the keystroke itself:
  // UI::executeCommand()'s named-command dispatch (M-x, Launchpad) falls
  // through to PatternEditor's own registry for any command name this
  // class doesn't define - a bare "kill-region" invoked while this widget
  // has focus would otherwise silently cut PatternEditor's own selection
  // instead of this widget's cell, exactly the same class of leak
  // move-row-up/move-row-down's own no-op definitions already guard against
  // below.
  keymap_.bind(KeyChord::pack('w', true, false, false, false), "kill-region"); // Ctrl-W
  keymap_.bind(KeyChord::pack('w', false, true, false, false), "kill-ring-save"); // Alt-W
  keymap_.bind(KeyChord::pack('y', true, false, false, false), "yank"); // Ctrl-Y

  // Shared by kill-region/kill-ring-save below: reads the cursor cell's
  // Pattern into cell_clipboard_/cell_clipboard_track_id_, or leaves the
  // clipboard untouched (returns false) if there's nothing valid to copy
  // (out of bounds - deliberately NOT reaching into the track's own
  // nested Effect children either, even though one can carry its own
  // per-scene Command automation - see getVisibleTrackIds()'s own comment
  // for why: a shared parent Effect has no single owning cell that data
  // could safely fold into).
  auto copy_cursor_cell = [this]() -> bool {
    auto & song = getController().getSong();
    auto track_ids = getVisibleTrackIds(song);
    if (cursor_track_index_ >= static_cast<int>(track_ids.size())) return false;
    if (cursor_scene_ >= static_cast<int>(song.getScenes().size())) return false;

    auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    auto track = song.getMasterTrack().getChildByInternalId(track_id);

    auto & scene = song.getScene(cursor_scene_);
    auto & patterns = scene.getPatternsByTrack();
    auto it = patterns.find(track_id);
    cell_clipboard_ = it != patterns.end() ? it->second : Pattern();
    cell_clipboard_track_id_ = track_id;
    cell_clipboard_tuning_ = track ? song.getTuningForTrack(*track) : song.getTuning();
    cell_clipboard_song_id_ = song.getInternalId();
    return true;
  };

  commands_.define("kill-region", [this, copy_cursor_cell]() {
    if (!copy_cursor_cell()) return;
    auto & song = getController().getSong();
    auto & scene = song.getScene(cursor_scene_);
    scene.setPatternForTrack(cell_clipboard_track_id_, Pattern());
    song.incVersion();
    getController().getUIEventQueue().push(make_unique<LogEvent>("Cell killed"));
  });

  commands_.define("kill-ring-save", [this, copy_cursor_cell]() {
    if (!copy_cursor_cell()) return;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Cell copied"));
  });

  commands_.define("yank", [this]() {
    if (!cell_clipboard_) {
      getController().getUIEventQueue().push(make_unique<LogEvent>("Clipboard empty"));
      return;
    }
    auto & song = getController().getSong();
    auto num_scenes = static_cast<int>(song.getScenes().size());
    if (cursor_scene_ > num_scenes) return; // shouldn't happen given the cursor's own clamp - defensive

    // Same song as the copy: paste back into the exact track it came
    // from, same as always - refusing (silently, matching this class's
    // own long-standing behavior) if that specific track was genuinely
    // deleted meanwhile. A different song (this app supports several
    // open at once - Controller::openSong()/switchToBuffer()): "the same
    // track" isn't a coherent target any more (track ids are one counter
    // shared across every open song, so cell_clipboard_track_id_ simply
    // can't exist here even though nothing was actually deleted) - fall
    // back to whatever cell the cursor is currently on instead.
    bool same_song = song.getInternalId() == cell_clipboard_song_id_;
    int target_track_id;
    if (same_song) {
      if (!song.getMasterTrack().getChildByInternalId(cell_clipboard_track_id_)) return;
      target_track_id = cell_clipboard_track_id_;
    } else {
      auto track_ids = getVisibleTrackIds(song);
      if (cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
      target_track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    }

    // A percussion cell can't land on a pitched column or vice versa (a
    // Note::getValue() means something different under each) - the same
    // check that also covers pasting between two songs written in
    // different temperaments, since Tuning distinguishes those too.
    auto target_track = song.getMasterTrack().getChildByInternalId(target_track_id);
    if (!target_track || song.getTuningForTrack(*target_track) != cell_clipboard_tuning_) {
      getController().getUIEventQueue().push(make_unique<LogEvent>("Cannot paste: incompatible tuning"));
      return;
    }

    // cursor_scene_ == num_scenes is the virtual, one-past-the-end row
    // (see ensureCursorVisible()'s own comment) - this is the one place
    // it actually gets instantiated, on the first real write into it, not
    // merely by navigating there (Enter) or scrolling past it.
    if (cursor_scene_ == num_scenes) song.addScene();

    auto & scene = song.getScene(cursor_scene_);
    scene.setPatternForTrack(target_track_id, *cell_clipboard_);
    song.incVersion();
    getController().getUIEventQueue().push(make_unique<LogEvent>("Cell pasted"));
  });

  // Deliberate no-ops, not left undefined: a Launchpad's CC91/92 ("move-
  // row-up"/"move-row-down") reach here via UI::executeCommand()'s
  // active-element-first dispatch while this widget has focus - without
  // an entry of its own, that dispatch falls through to PatternEditor's
  // *own* "move-row-up"/"move-row-down" (its own fallback for a command
  // the active element doesn't own), which would move the real playhead
  // even though PatternMatrix's own cursor is meant to stay local and
  // passive. Defining (even empty) commands here absorbs the dispatch
  // instead.
  commands_.define("move-row-up", []() {});
  commands_.define("move-row-down", []() {});

  assertCommandBindingsValid();
}

void
PatternMatrix::ensureCursorVisible(int visible_rows, int visible_cols, int num_scenes, int num_tracks) {
  // +1: the cursor (and the scrolled viewport) may reach exactly one row
  // past the last real Scene - a virtual, not-yet-instantiated row (see
  // offerInput()'s own NCKEY_ENTER/NCKEY_DOWN comments and the "yank"
  // command below, the only thing that actually creates it). Folding this
  // into one extra slot here, rather than a separate "is this the virtual
  // row" special case, is also what makes a brand new song (zero Scenes)
  // show one real, navigable row instead of nothing at all.
  auto scene_slots = num_scenes + 1;
  cursor_scene_ = clamp(cursor_scene_, 0, scene_slots - 1);
  if (cursor_scene_ < scroll_row_) scroll_row_ = cursor_scene_;
  if (visible_rows > 0 && cursor_scene_ >= scroll_row_ + visible_rows) scroll_row_ = cursor_scene_ - visible_rows + 1;
  scroll_row_ = clamp(scroll_row_, 0, max(0, scene_slots - visible_rows));

  if (num_tracks <= 0) {
    cursor_track_index_ = 0;
    scroll_col_ = 0;
  } else {
    cursor_track_index_ = clamp(cursor_track_index_, 0, num_tracks - 1);
    if (cursor_track_index_ < scroll_col_) scroll_col_ = cursor_track_index_;
    if (visible_cols > 0 && cursor_track_index_ >= scroll_col_ + visible_cols) scroll_col_ = cursor_track_index_ - visible_cols + 1;
    scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));
  }
}

bool
PatternMatrix::offerInput(const InputEvent & input) {
  if (dispatchCommand(input)) return true;
  if (input.getKind() == InputEvent::Kind::RELEASE) return false;

  auto & song = getController().getSong();
  auto track_ids = getVisibleTrackIds(song);
  auto num_scenes = static_cast<int>(song.getScenes().size());
  auto num_tracks = static_cast<int>(track_ids.size());

  if (input.getId() == NCKEY_ENTER) {
    // cursor_scene_ == num_scenes (the one-past-the-end row - see the
    // clamp below) is a real, valid target here, not refused: it just
    // moves the playhead/track selection there, same as PatternEditor's
    // own row navigation already tolerates running off the end of the
    // last real Scene ("normal, not special" - Controller::
    // moveEditPosition()'s own comment). Deliberately NOT instantiating a
    // Scene on commit - that stays yank's own job (see below), keeping
    // "look at/jump to this position" and "put real content here" as two
    // separate actions, the same distinction PatternEditor's own
    // navigation-vs-editing already draws.
    if (commit_callback_ && cursor_track_index_ < num_tracks) {
      commit_callback_(track_ids[static_cast<size_t>(cursor_track_index_)], cursor_scene_);
    }
    return true;
  }

  if (input.getId() == NCKEY_UP) cursor_scene_--;
  else if (input.getId() == NCKEY_DOWN) cursor_scene_++;
  else if (input.getId() == NCKEY_LEFT) cursor_track_index_--;
  else if (input.getId() == NCKEY_RIGHT) {
    // Already on the last (rightmost) track column - nowhere further
    // right to go in the overview itself, so this leaves it and hands
    // focus back to PatternEditor instead of a no-op clamp (the row
    // axis's own mirror image of the "move past the last row extends the
    // song" gesture above, and of PatternEditor's own leftward "nowhere
    // further left, enter the overview" edge).
    if (cursor_track_index_ >= num_tracks - 1 && exit_right_callback_) {
      exit_right_callback_();
      return true;
    }
    cursor_track_index_++;
  }
  else return false;

  // The row axis allows exactly one position past the last real Scene
  // (num_scenes itself) - see ensureCursorVisible()'s own comment for why
  // this doesn't turn into unbounded scrolling.
  cursor_scene_ = clamp(cursor_scene_, 0, num_scenes);
  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));
  return true;
}

bool
PatternMatrix::render(const StyleProvider & styles, bool refresh, bool focused) {
  auto & song = getController().getSong();
  auto track_ids = getVisibleTrackIds(song);
  auto num_scenes = static_cast<int>(song.getScenes().size());
  auto num_tracks = static_cast<int>(track_ids.size());

  auto [ rows, cols ] = getDim();
  if (rows < 1 || cols < 1) return false;
  auto visible_rows = max(0, rows - 1); // row 0 is the column-header row
  // Each track gets its own cell plus one blank separator column after it -
  // not just visual breathing room: it's also where a two-digit header
  // ordinal's second digit lands (see the header loop below), and where a
  // data cell's glyph can safely spill if the terminal font renders it
  // wider than one cell (an East-Asian-Width "ambiguous" glyph, which
  // notcurses/this renderer otherwise has no way to detect ahead of time -
  // rather than the glyph visibly overlapping the next track's own cell,
  // it now only ever overlaps its own reserved blank column).
  constexpr int kColWidth = 2;
  auto visible_cols = cols / kColWidth;

  ensureCursorVisible(visible_rows, visible_cols, num_scenes, num_tracks);

  auto & playback_info = getController().getPlaybackInfo();
  auto playing_scene = playback_info.getPatternIndex();
  auto new_version = song.getMajorVersion();

  if (!refresh && new_version == current_song_version_ && playing_scene == current_playing_scene_ &&
      cursor_scene_ == current_cursor_scene_ && cursor_track_index_ == current_cursor_track_index_ &&
      scroll_row_ == current_scroll_row_ && scroll_col_ == current_scroll_col_ &&
      focused == current_focused_) {
    return false;
  }
  current_song_version_ = new_version;
  current_playing_scene_ = playing_scene;
  current_cursor_scene_ = cursor_scene_;
  current_cursor_track_index_ = cursor_track_index_;
  current_scroll_row_ = scroll_row_;
  current_scroll_col_ = scroll_col_;
  current_focused_ = focused;

  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  erase();

  // Every id in track_ids is already color-eligible (getVisibleTrackIds()
  // filtered out anything that isn't), so every column below has a real
  // identity color to work with - no grey fallback needed any more.
  SongStructure structure(song);
  const Color kWhite(255, 255, 255);

  // This track's identity color as *foreground* text - same hue as
  // getColor() (factored out as VisibleTrackInfo::getHue() so the two
  // don't duplicate the golden-angle formula), but near-fully saturated
  // rather than getColor()'s fixed 35%/42%: that combination is tuned for
  // a *background* bar with white text over it (see VisibleTrackInfo::
  // getColor()'s own comment) and read as too faint used as foreground
  // glyph/text color against the plain window background instead - used
  // for both the header digits and the data glyphs below. Lightness is
  // tuned independently from Launchpad's own OVERVIEW LEDs
  // (LaunchpadManager.cpp) - a directly-emitted LED pixel at a given
  // lightness reads brighter than the same value does as terminal glyph
  // text, so the two surfaces no longer share one constant here.
  auto identity_color = [&](int track_id) {
    return Color::fromHSL(structure.getBaselineInfo(track_id).getHue(), 0.8f, 0.45f);
  };

  // Header row: no background bar (tried, reverted - the color alone,
  // bold, on the plain window background reads clearly enough without
  // needing to paint the whole cell).
  for (auto vc = 0; vc < visible_cols && scroll_col_ + vc < num_tracks; vc++) {
    auto track_id = track_ids[static_cast<size_t>(scroll_col_ + vc)];
    auto & baseline = structure.getBaselineInfo(track_id);
    auto x = vc * kColWidth;
    // Blank the separator column *first* - ncplane_erase() only clears
    // cell content, not color, so this needs explicit painting the same
    // way the data loop's own separator cells do (see its comment) - a
    // two-digit ordinal below then overwrites this with its own second
    // digit when it needs to.
    setFgColor(styles.window_fg_color);
    setBgColor(styles.window_bg_color);
    putstr(0, x + 1, " ");
    setFgColor(identity_color(track_id));
    setBold(true);
    // Safe up to 99 tracks; `% 100` guards the (unrealistic) case beyond
    // that from overflowing into the next track's own column.
    putstr(0, x, to_string(baseline.color_ordinal_ % 100));
    setBold(false);
  }

  for (auto vr = 0; vr < visible_rows; vr++) {
    auto scene_idx = scroll_row_ + vr;
    // scene_idx == num_scenes (exactly one past the end) is the virtual
    // row - see ensureCursorVisible()'s own comment - rendered the same
    // way any other all-empty Scene would be: Song::getScene() already
    // returns a shared empty Scene for an out-of-range index, so every
    // column below just reads as empty with no special-casing needed here.
    if (scene_idx > num_scenes) break;
    auto & scene = song.getScene(scene_idx);
    auto & patterns = scene.getPatternsByTrack();
    auto is_playing_row = scene_idx == playing_scene;

    for (auto vc = 0; vc < visible_cols && scroll_col_ + vc < num_tracks; vc++) {
      auto track_index = scroll_col_ + vc;
      auto track_id = track_ids[static_cast<size_t>(track_index)];
      auto glyph_color = identity_color(track_id);

      // Plain window background - no per-track tint (only the playhead
      // row gets any background treatment at all, below).
      auto bg = styles.window_bg_color;

      string glyph = " ";
      Color fg = glyph_color;
      // A DrumMachineTrack column renders exactly like any other track
      // here - its step content is an ordinary per-scene Pattern now, the
      // same as any other track's.
      auto it = patterns.find(track_id);
      if (it != patterns.end() && !it->second.isEmpty()) {
        // A Pattern can be non-empty from note-offs/aftertouch/Command
        // data alone, with no note-on anywhere in it - hasSoundingNote()
        // tells that apart from real sound-producing content.
        glyph = it->second.hasSoundingNote() ? "\U0001F5CF" : "\U0001F5CC"; // 🗏 PAGE : 🗌 EMPTY PAGE
      }
      // else: genuinely empty cell - stays blank, no glyph at all.

      if (is_playing_row) bg = bg.blend(0.15f, kWhite);

      // Distracting otherwise, and ambiguous about which window a
      // kill-ring-save/yank would actually target - see PatternEditor's
      // own equivalent gating for the same reasoning.
      auto is_cursor_cell = focused && scene_idx == cursor_scene_ && track_index == cursor_track_index_;
      if (is_cursor_cell) {
        fg = styles.highlight_fg_color;
        bg = styles.highlight_bg_color;
      }

      setFgColor(fg);
      setBgColor(bg);
      auto x = vc * kColWidth;
      putstr(1 + vr, x, glyph);
      // Always explicitly painted (not left to erase()'s fallback - see
      // the header loop's own comment on why that doesn't actually work)
      // with the same bg as the glyph cell, so the playhead wash/cursor
      // highlight reads as one continuous span rather than stopping one
      // character short of the next track's own column.
      putstr(1 + vr, x + 1, " ");
    }
  }

  return true;
}
