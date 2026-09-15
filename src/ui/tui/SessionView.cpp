#include "SessionView.h"

#include "../../playback/InputEvent.h"
#include "../../playback/LogEvent.h"
#include "../KeyChord.h"
#include "../StyleProvider.h"
#include "../../Controller.h"
#include "../../model/Song.h"
#include "../../model/ArrangementOps.h"
#include "../../model/LeafTrack.h"
#include "../../model/Clip.h"
#include "../../model/SongStructure.h"
#include "../../util/Utf8.h"

#include <algorithm>
#include <memory>
#include <fmt/core.h>

using namespace std;

namespace {

const LeafTrack *
asLeafTrack(const Song & song, int track_id) {
  return dynamic_cast<const LeafTrack *>(song.getMasterTrack().getChildByInternalId(track_id));
}

// Same "each file keeps its own small dB helper" convention model/
// LeafTrack.cpp's own dbToLinear()/LaunchpadManager.cpp's own
// linearToDb() already use - LeafTrack::getSends() is always the plain
// linear multiplier (SendLevels.h's own doc comment), dB is only ever a
// display/control-surface unit one layer up from there.
float linearToDb(float linear) { return linear <= 0.00001f ? -100.0f : 20.0f * log10f(linear); }
// Clamped so a 3-character column (sign + 2 digits) always fits a real
// number rather than needing "-inf"/a wider column - -99 reads as
// "effectively off" just as clearly as the true floor would.
float clampedDb(float db) { return std::max(db, -99.0f); }

const Color kWhite(255, 255, 255);
// Cursor highlight for every cursor-addressable row except a clip cell
// (which brightens its own track color instead - see render()'s own
// comment) - styles.highlight_bg_color is bright green, easily confused
// with a clip row's own track-identity-colored background whenever that
// happens to be green too.
const Color kBrightGrey(200, 200, 200);

}

SessionView::SessionView(UIPlane & parent) : UIElement(parent) {
  // Mute/Solo apply to the cursor's own column (a per-track state, not
  // tied to any particular row), same as the 'l' loop toggle stays a
  // manual offerInput() branch below rather than a command - there's
  // nothing else in this app either binding is likely to collide with.
  commands_.define("toggle-mute", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    bool now_muted = getController().toggleTrackMuted(track_id);
    getController().getUIEventQueue().push(std::make_unique<LogEvent>(now_muted ? "Muted" : "Unmuted"));
  });
  commands_.define("toggle-solo", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    bool now_solo = getController().toggleTrackSolo(track_id);
    getController().getUIEventQueue().push(std::make_unique<LogEvent>(now_solo ? "Solo on" : "Solo off"));
  });
  // 'm'/'s' are this view's own shortcuts; backslash/Ctrl-backslash match
  // PatternEditor's own toggle-mute/toggle-solo bindings, so either works
  // regardless of which of the two views is currently focused.
  keymap_.bind(KeyChord::pack('m', false, false, false, false), "toggle-mute");
  keymap_.bind(KeyChord::pack('s', false, false, false, false), "toggle-solo");
  keymap_.bind(KeyChord::pack('\\', false, false, false, false), "toggle-mute");
  keymap_.bind(KeyChord::pack('\\', true, false, false, false), "toggle-solo");

  // Only meaningful on a populated clip row; a no-op anywhere else, same
  // "always does something or nothing, never falls through" precedent
  // 'l' (loop toggle) already follows. A real, unprompted deletion (no
  // confirmation dialog) - see the keymap_.bind() calls below for which
  // keys reach it and why.
  commands_.define("delete-clip", [this]() {
    if (rowKindFor(cursor_row_) != RowKind::CLIP) return;
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    auto & clips = song.getClips(track_id);
    auto clip_row = kLogicalToPhysical[cursor_row_]; // a CLIP row's own physical offset doubles as its clip-list index
    // Content-aware, not just in-bounds: an empty filler (Song::
    // ensureClipAt()'s own "hole", or a genuinely out-of-bounds row) reads
    // as "no clip here" either way - erasing a filler would shift every
    // later clip's own index down, silently misaligning every other
    // track's own scene rows against it.
    if (clip_row < 0 || static_cast<size_t>(clip_row) >= clips.size() || clips[static_cast<size_t>(clip_row)].isEmpty()) return; // no clip here to delete
    auto clip_id = clips[static_cast<size_t>(clip_row)].getId();
    auto name = clips[static_cast<size_t>(clip_row)].getName();
    // Clears any live preview/edit focus on the clip being deleted -
    // otherwise Controller::getFocusedClip() would keep pointing at an id
    // nothing resolves to any more (harmless - every lookup already
    // tolerates a stale/dangling clip id - but there's no reason to leave
    // it dangling when the clip's own removal is the very moment that
    // makes it stale).
    if (getController().getFocusedClipTrackId() == track_id && getController().getFocusedClip() == clip_id) {
      getController().clearFocusedClip();
    }
    deleteClip(song, track_id, clip_row);
    auto text = "Deleted clip: " + (name.empty() ? string("(unnamed)") : name);
    getController().getUIEventQueue().push(std::make_unique<LogEvent>(std::move(text)));
  });
  // Del, Backspace, and Ctrl-K all reach it - the same three keys this
  // app already treats as "delete something at the cursor" elsewhere
  // (Backspace/Del clearing note content, Ctrl-K placing a stop instance,
  // both in PatternEditor; ArrangementGrid's own Backspace doing the
  // same) - rather than a dedicated modifier chord of its own.
  keymap_.bind(KeyChord::pack(NCKEY_DEL, false, false, false, false), "delete-clip");
  keymap_.bind(KeyChord::pack(NCKEY_BACKSPACE, false, false, false, false), "delete-clip");
  keymap_.bind(KeyChord::pack('k', true, false, false, false), "delete-clip"); // Ctrl-K
  assertCommandBindingsValid();
}

SessionView::RowKind
SessionView::rowKindFor(int logical_row) const {
  if (logical_row == 0) return RowKind::HEADER;
  if (logical_row <= kClipRowCount) return RowKind::CLIP;
  if (logical_row == kClipRowCount + 1) return RowKind::SENDS;
  return RowKind::DIRECTION;
}

void
SessionView::ensureCursorVisible(int visible_rows, int visible_cols, int num_tracks) {
  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));
  cursor_row_ = clamp(cursor_row_, 0, kLogicalRowCount - 1);

  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));
  scroll_row_ = clamp(scroll_row_, 0, max(0, kPhysicalRowCount - visible_rows));

  if (cursor_track_index_ < scroll_col_) scroll_col_ = cursor_track_index_;
  if (visible_cols > 0 && cursor_track_index_ >= scroll_col_ + visible_cols) scroll_col_ = cursor_track_index_ - visible_cols + 1;
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_tracks - visible_cols));

  auto cursor_physical = kLogicalToPhysical[cursor_row_];
  if (cursor_physical < scroll_row_) scroll_row_ = cursor_physical;
  if (visible_rows > 0 && cursor_physical >= scroll_row_ + visible_rows) scroll_row_ = cursor_physical - visible_rows + 1;
  scroll_row_ = clamp(scroll_row_, 0, max(0, kPhysicalRowCount - visible_rows));
}

void
SessionView::startClipRename(const Song & song, const std::vector<int> & track_ids) {
  if (getPlane().readerActive()) return;
  if (rowKindFor(cursor_row_) != RowKind::CLIP) return; // not on a clip row at all
  if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
  auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
  auto & clips = song.getClips(track_id);
  auto clip_row = kLogicalToPhysical[cursor_row_]; // a CLIP row's own physical offset doubles as its clip-list index (both 0..kClipRowCount-1)
  // Same content-aware "an empty filler reads as no clip here" reasoning
  // as delete-clip above - nothing to give a name to yet.
  if (clip_row < 0 || static_cast<size_t>(clip_row) >= clips.size() || clips[static_cast<size_t>(clip_row)].isEmpty()) return; // no clip here to rename

  auto physical_row = clip_row - scroll_row_ + 1; // +1 for the header row
  auto [ rows, cols ] = getDim();
  if (physical_row < 0 || physical_row >= rows) return; // off-screen - shouldn't happen given ensureCursorVisible(), a cosmetic nuisance if it ever does
  auto x = cursor_track_index_ - scroll_col_;
  if (x < 0) return; // column itself scrolled off - same defensive case
  auto col_x = x * (kColWidth + 1);

  renaming_clip_row_ = clip_row;

  getPlane().showReader("", physical_row, col_x, 1, kColWidth, clips[static_cast<size_t>(clip_row)].getName());

  // TerminalUI::showReader()'s own ncplane_erase_region() call erases
  // from col_x all the way to the *plane's* right edge, not just this one
  // clip's own kColWidth footprint - fine for ArrangementGrid's own section
  // rename (a title row spans the whole plane width, nothing else sits on
  // it), but this cell is one column among several tracks sharing the
  // same row, and every later track's own clip cell on this same row
  // just got wiped. A real render() pass repaints all of it - safe to
  // call directly here since the reader's own child plane, stacked on
  // top of just its own [col_x, col_x + kColWidth) footprint, is
  // unaffected by whatever the parent plane underneath it gets redrawn
  // to (same reasoning PatternEditor's own startTrackNameEdit() already
  // documents for its own narrower-than-the-row reader).
  if (last_styles_) render(*last_styles_, true, current_focused_);

  // That repaint just redrew this same clip's own current name/icons
  // straight back into the cell the reader is editing, visible right
  // through the reader's own cells wherever nothing's been typed yet
  // (TerminalUI::showReader()'s ncplane_set_base(..., "", ...) doesn't
  // paint over cells nothing ever explicitly writes to) - blank it again,
  // the same reasoning ArrangementGrid::startSectionRename() already
  // documents for its own single-column case.
  setFgColor(0, 0, 0);
  setBgColor(0, 0, 0);
  putstr(physical_row, col_x, string(static_cast<size_t>(kColWidth), ' '));
}

void
SessionView::startTrackRename(const Song & song, const std::vector<int> & track_ids) {
  if (getPlane().readerActive()) return;
  if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
  auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
  auto * track = song.getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;

  auto x = cursor_track_index_ - scroll_col_;
  if (x < 0) return; // column itself scrolled off
  auto col_x = x * (kColWidth + 1);

  // The editable span excludes the leading "T<N> " structural label
  // (N = color_ordinal_) - PatternEditor's own startTrackNameEdit() draws
  // that same prefix and leaves it out of what's editable too - and the
  // trailing " MS" Mute/Solo pair, matching render()'s own header layout
  // exactly so the reader lands right over the name it's replacing.
  constexpr int kMuteSoloWidth = 3; // " MS"
  SongStructure structure(song);
  auto prefix = "T" + std::to_string(structure.getBaselineInfo(track_id).color_ordinal_) + " ";
  auto name_area_width = kColWidth - kMuteSoloWidth;
  auto prefix_width = std::min(static_cast<int>(prefix.size()), name_area_width);
  auto edit_col = col_x + prefix_width;
  auto edit_width = std::max(name_area_width - prefix_width, 1);

  renaming_track_id_ = track_id;

  getPlane().showReader("", 0, edit_col, 1, edit_width, track->getName());

  // Same reasoning startClipRename() documents above: showReader()'s own
  // erase_region call erases the rest of this row across every later
  // track's own header too, so a real render() pass repaints all of it,
  // then this field's own span is blanked again on top (it would
  // otherwise still show the track's current name straight through the
  // reader's own untyped cells).
  if (last_styles_) render(*last_styles_, true, current_focused_);

  setFgColor(0, 0, 0);
  setBgColor(0, 0, 0);
  putstr(0, edit_col, string(static_cast<size_t>(edit_width), ' '));
}

bool
SessionView::offerInput(const InputEvent & input) {
  // const - Song::getClips() has a non-const overload that inserts an
  // empty entry for a track that doesn't have one yet (needed for actual
  // mutation elsewhere, e.g. ArrangementOps.cpp); most of this method
  // only reads, so binding const here keeps every getClips() call below
  // side-effect free regardless of which overload it'd otherwise resolve
  // to. The two branches that actually write (a clip rename commit, the
  // loop toggle) each fetch their own non-const reference locally instead
  // of widening this one.
  const Song & song = getController().getSong();
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  // Mirrors PatternEditor::offerInput()'s/ArrangementGrid::offerInput()'s
  // own reader-active handling: while the clip-rename or track-rename
  // editor (startClipRename()/startTrackRename()) is open, Enter commits
  // and Ctrl-g cancels; everything else goes to the reader instead of any
  // of this class's own keybinding dispatch/manual handling below.
  if (getPlane().readerActive()) {
    if (input.getId() == NCKEY_ENTER) {
      auto text = getPlane().closeReader();
      if (renaming_clip_row_ >= 0 && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
        auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
        auto & mutable_song = getController().getSong(); // non-const - this branch genuinely writes, unlike the rest of this method (see `song`'s own comment above)
        auto & clips = mutable_song.getClips(track_id);
        if (static_cast<size_t>(renaming_clip_row_) < clips.size()) {
          clips[static_cast<size_t>(renaming_clip_row_)].setName(std::move(text));
          mutable_song.incVersion();
        }
      } else if (renaming_track_id_ >= 0) {
        auto & mutable_song = getController().getSong(); // non-const, see above
        auto * track = mutable_song.getMasterTrack().getChildByInternalId(renaming_track_id_);
        if (track) {
          track->setName(std::move(text));
          mutable_song.incVersion();
        }
      }
      renaming_clip_row_ = -1;
      renaming_track_id_ = -1;
      force_redraw_ = true;
      return true;
    } else if (input.hasCtrl() && input.getId() == 'g') {
      getPlane().closeReader();
      renaming_clip_row_ = -1;
      renaming_track_id_ = -1;
      force_redraw_ = true;
      return true;
    } else {
      return getPlane().offerInput(input);
    }
  }

  if (dispatchCommand(input)) return true;
  if (input.getKind() == InputEvent::Kind::RELEASE) return false;

  // Navigation, plus the manual (non-command) part of this widget's own
  // input handling: Enter (clip focus toggle, on a clip row), F2 (rename
  // the clip under the cursor), 'l' (toggle its own loop flag) - Mute/
  // Solo are dispatchCommand()'s job now, handled above via this
  // constructor's own toggle-mute/toggle-solo bindings.
  if (input.getId() == NCKEY_UP) cursor_row_--;
  else if (input.getId() == NCKEY_DOWN) cursor_row_++;
  else if (input.getId() == NCKEY_LEFT) cursor_track_index_--;
  else if (input.getId() == NCKEY_RIGHT) cursor_track_index_++;
  else if (input.getId() == NCKEY_PGUP) cursor_row_ -= getDim().first;
  else if (input.getId() == NCKEY_PGDOWN) cursor_row_ += getDim().first;
  else if (input.getId() == NCKEY_F02) {
    // Renames whatever's actually under the cursor - the track, on the
    // header row; the clip otherwise (startClipRename() itself no-ops on
    // a row that isn't a populated clip slot).
    if (rowKindFor(cursor_row_) == RowKind::HEADER) startTrackRename(song, track_ids);
    else startClipRename(song, track_ids);
    return true;
  } else if (input.getId() == 'l' && !input.hasCtrl() && !input.hasAlt()) {
    // Loop toggle - only meaningful on a clip row that actually has a
    // clip; a no-op (but still consumed) anywhere else, same "always
    // does something or nothing, never falls through" precedent
    // ArrangementGrid's own Enter handling already has.
    if (rowKindFor(cursor_row_) == RowKind::CLIP && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
      auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
      auto & clips = song.getClips(track_id);
      auto clip_row = kLogicalToPhysical[cursor_row_]; // a CLIP row's own physical offset doubles as its clip-list index
      if (clip_row >= 0 && static_cast<size_t>(clip_row) < clips.size() && !clips[static_cast<size_t>(clip_row)].isEmpty()) {
        auto & mutable_song = getController().getSong(); // non-const - this branch genuinely writes, unlike the rest of this method (see `song`'s own comment above)
        auto & clip = mutable_song.getClips(track_id)[static_cast<size_t>(clip_row)];
        clip.setLooping(!clip.isLooping());
        mutable_song.incVersion();
      }
    }
    return true;
  } else if (input.getId() == NCKEY_ENTER) {
    // Acts exactly like a Launchpad Session view pad press landing on this
    // same cell (LaunchpadManager::triggerSessionClip(), via
    // trigger_callback_ - see setTriggerCallback()'s own comment) - an
    // empty row stops/cancels whatever the track is doing, the same as
    // pressing an unassigned pad would, so this is called unconditionally
    // on any CLIP row rather than only a populated one.
    auto kind = rowKindFor(cursor_row_);
    if (kind == RowKind::CLIP && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks && trigger_callback_) {
      auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
      trigger_callback_(track_id, kLogicalToPhysical[cursor_row_]);
    }
    // HEADER/SENDS/DIRECTION: read-only for now - a no-op, still consumed.
    return true;
  }
  else return false;

  cursor_track_index_ = clamp(cursor_track_index_, 0, max(0, num_tracks - 1));
  cursor_row_ = clamp(cursor_row_, 0, kLogicalRowCount - 1);

  // Song::getCurrentTrackId() sync - see PatternEditor::render()'s own
  // equivalent for why this matters (a command like merge-clip-to-
  // background needs to find the right track regardless of which widget
  // actually moved the cursor last).
  if (cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
    getController().getSong().setCurrentTrackId(track_ids[static_cast<size_t>(cursor_track_index_)]); // non-const, see `song`'s own comment above
  }

  return true;
}

bool
SessionView::render(const StyleProvider & styles, bool refresh, bool focused) {
  last_styles_ = &styles; // see startClipRename()'s own comment - its only source of one
  const Song & song = getController().getSong(); // see offerInput()'s own comment on why const
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  auto [ rows, cols ] = getDim();
  if (rows < 2 || cols < 1) return false;

  auto visible_rows = max(0, rows - 1); // row 0 is the header, never scrolled
  auto visible_cols = max(0, cols) / (kColWidth + 1);

  ensureCursorVisible(visible_rows, visible_cols, num_tracks);

  // Which clip (if any) is currently focused for editing - shown as a
  // marker on its own row below, independent of cursor position. Read
  // once here (not per-cell) since it's the same value for every column.
  auto focused_clip_id = getController().getFocusedClip();

  auto new_version = song.getMajorVersion();
  // Coarse on purpose - several tracks can each have their own in-flight
  // take now, not just one, so there's no single (track_id, clip_index)
  // pair left to compare against a cached one; any recording activity at
  // all forces a redraw instead of trying to detect exactly what changed
  // (the per-cell check below still resolves the real, current per-track
  // detail every time this does redraw).
  bool is_session_recording = getController().isAnySessionRecording();
  if (!refresh && !force_redraw_ && new_version == current_song_version_ &&
      cursor_track_index_ == current_cursor_track_index_ && cursor_row_ == current_cursor_row_ &&
      scroll_col_ == current_scroll_col_ && scroll_row_ == current_scroll_row_ &&
      focused == current_focused_ && focused_clip_id == current_focused_clip_id_ &&
      is_session_recording == current_session_recording_) {
    return false;
  }
  force_redraw_ = false;
  current_focused_clip_id_ = focused_clip_id;
  current_session_recording_ = is_session_recording;
  current_song_version_ = new_version;
  current_cursor_track_index_ = cursor_track_index_;
  current_cursor_row_ = cursor_row_;
  current_scroll_col_ = scroll_col_;
  current_scroll_row_ = scroll_row_;
  current_focused_ = focused;

  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  erase();
  // erase() alone leaves this plane's own base cell showing through
  // wherever nothing below explicitly writes a cell (its own semi-
  // transparent debug base, UIPlane::createChild()) - this widget shares
  // its exact screen rect with pattern_editor_ (see UI::layout()), so
  // any such gap would show pattern_editor_'s last real content bleeding
  // through underneath rather than a plain blank. fill() (plain spaces,
  // the color just set) guarantees every cell in the plane is genuinely
  // opaque before anything more specific gets drawn over it - the same
  // reasoning ArrangementGrid's own per-column loop satisfies by simply
  // never leaving a column unwritten in the first place (it always draws
  // a blank/background glyph for an out-of-range column rather than
  // stopping early - see its own render()).
  fill();

  SongStructure structure(song);
  auto cursor_physical = kLogicalToPhysical[cursor_row_];

  for (auto vc = 0; vc < visible_cols; vc++) {
    auto track_index = scroll_col_ + vc;
    if (track_index >= num_tracks) break;
    auto track_id = track_ids[static_cast<size_t>(track_index)];
    auto x = vc * (kColWidth + 1);
    auto * leaf = asLeafTrack(song, track_id);
    auto & clips = song.getClips(track_id);

    // Header (row 0, never part of the scrolled/cursor-addressable row
    // axis below) - the exact same "T<N> name" label PatternEditor's own
    // renderHeading() draws (N = color_ordinal_, not track_index - the
    // same stable-across-scroll count that also picks the track's own
    // identity color), upright T<N> prefix followed by the track's own
    // name in italic, then a trailing "MS" pair showing this track's own
    // Mute/Solo state (bright when on, dim otherwise - there's no
    // dedicated Mute/Solo row any more, see this class's own header
    // comment). Plain accent color for the name, like every other
    // non-clip cell in this column - the track's own identity color is
    // reserved for its clip cells alone.
    constexpr int kMuteSoloWidth = 3; // " MS"
    auto name_width = kColWidth - kMuteSoloWidth;
    // -1 is the header row's own physical value (kLogicalToPhysical's own
    // comment) - never a real physical_row inside the loop below, so this
    // is the header's one and only cursor check.
    bool header_is_cursor = focused && track_index == cursor_track_index_ && cursor_physical == -1;
    setFgColor(header_is_cursor ? styles.highlight_fg_color : styles.window_accent_fg_color);
    setBgColor(header_is_cursor ? kBrightGrey : styles.window_accent_bg_color); // dark grey backdrop normally, brightened the same way every other cursor-addressable row is when the cursor's actually here
    putstr(0, x, string(static_cast<size_t>(kColWidth), ' ')); // opaque header background first, across the whole column
    auto prefix = "T" + std::to_string(structure.getBaselineInfo(track_id).color_ordinal_);
    auto friendly_name = (leaf && !leaf->getName().empty()) ? " " + leaf->getName() : string();
    auto header = Utf8::padToWidth(Utf8::truncateToWidth(prefix + friendly_name, name_width), name_width);
    auto upright_len = std::min(header.size(), prefix.size());
    putstr(0, x, header.substr(0, upright_len));
    if (upright_len < header.size()) {
      setItalic(true);
      putstr(0, x + static_cast<int>(upright_len), header.substr(upright_len));
      setItalic(false);
    }
    bool muted = leaf && leaf->isMuted();
    bool solo = leaf && leaf->isSolo();
    // The leading space of " MS" is left to fill()'s own blank - only the
    // two letters need their own color.
    setFgColor(muted ? Color(255, 90, 90) : styles.window_border_color);
    putstr(0, x + name_width + 1, "M");
    setFgColor(solo ? Color(255, 220, 90) : styles.window_border_color);
    putstr(0, x + name_width + 2, "S");

    for (auto vr = 0; vr < visible_rows; vr++) {
      auto physical_row = scroll_row_ + vr;
      auto y = 1 + vr;
      bool is_cursor_cell = focused && track_index == cursor_track_index_ && physical_row == cursor_physical;

      // The Sends value row mixes a cursive unit label with plain-weight
      // numbers, which a single putstr call can't do - handled directly
      // here (two calls, two styles) rather than through the shared
      // text/pad/put path every other row below shares.
      if (physical_row == 10) {
        Color row_fg = is_cursor_cell ? styles.highlight_fg_color : styles.window_fg_color;
        Color row_bg = is_cursor_cell ? kBrightGrey : styles.window_bg_color;
        setFgColor(row_fg);
        setBgColor(row_bg);
        putstr(y, x, string(static_cast<size_t>(kColWidth), ' ')); // opaque row background first
        if (leaf) {
          auto & sends = leaf->getSends();
          setItalic(true);
          putstr(y, x, " dB   "); // centered under "Sends" above it, nudged one column left of dead-center
          setItalic(false);
          auto values = fmt::format("{:>4.0f}{:>4.0f}{:>4.0f}",
            clampedDb(linearToDb(sends.main)), clampedDb(linearToDb(sends.a)), clampedDb(linearToDb(sends.b)));
          putstr(y, x + 6, values);
        }
        continue;
      }

      // A clip row's own trailing repeat glyph (U+21BB) renders wider in
      // this terminal than this codebase's own width tools (Utf8::
      // displayWidth(), and notcurses's own internal column-advance) can
      // account for - the same "ambiguous width" mismatch documented
      // elsewhere in this codebase, just not one this file can route
      // around by avoiding the glyph entirely, since a loop indicator is
      // the whole point here. Rather than trying to reserve its exact
      // real column count (unknowable from in here) and risk a gap at
      // the end regardless of which way the miscount goes, the row's own
      // background is painted as an opaque, plain-ASCII-measured full
      // width first, then the actual icon/name content drawn on top of
      // it - so however many real columns the glyph actually consumes,
      // there's no un-colored sliver left showing through underneath it.
      if (physical_row < kClipRowCount) {
        auto clip_row = static_cast<size_t>(physical_row);
        Color row_fg = Color(255, 255, 255), row_bg = styles.window_bg_color;
        string text;
        // Content-aware, not just in-bounds - holes are allowed (Song::
        // ensureClipAt()), so an in-bounds but still-empty filler renders
        // as the plain "nothing here" stop icon below, same as a row
        // genuinely past the list's own end.
        bool has_real_clip = clip_row < clips.size() && !clips[clip_row].isEmpty();
        if (has_real_clip) {
          auto & clip = clips[clip_row];
          auto name = clip.getName().empty() ? "(unnamed)" : clip.getName();
          // Leading marker: whether this clip is the one currently
          // focused for editing (Controller::getFocusedClip(), set by
          // Record Arm's own drum-machine-clip repurposing - Controller.cpp's
          // "toggle-record-arm"), a blank space otherwise so the play glyph/
          // name still line up. Plain ASCII, not a Unicode glyph -
          // "ambiguous width" characters silently render as two columns
          // on plenty of terminal fonts, breaking this fixed-width
          // layout; confirmed the hard way earlier switching this marker
          // away from one for the same reason.
          auto marker = clip.getId() == focused_clip_id ? "*" : " ";
          // Play icon (small triangle, single-slot-wide - the same glyph
          // PatternEditor's own collapse toggle uses) always shown for a
          // real clip; the repeat icon is a separate, additional glyph at
          // the row's own far right, shown only when the clip loops.
          // Reserves 2 trailing columns for the loop icon below - one for
          // the glyph itself, one of slack in case it renders wider here
          // than this row's own background-painting already accounts
          // for, so an over-wide render spills into blank space of its
          // own row rather than the divider column just past it.
          auto clip_name_width = kColWidth - 5; // marker + "▸ " + the 2 trailing columns
          auto name_field = Utf8::padToWidth(Utf8::truncateToWidth(name, clip_name_width), clip_name_width);
          text = fmt::format("{}▸ {}", marker, name_field);
          row_bg = structure.getBaselineInfo(track_id).getColor();
        } else {
          text = " ⏹";
          // Dim - an empty slot is background information, not something
          // to draw the eye the way a real clip's own bright white does -
          // but not window_border_color's own near-invisible divider
          // shade either, halfway to plain text gray instead so the glyph
          // still reads clearly as a real stop icon, not a smudge.
          row_fg = styles.window_fg_color.blend(0.5f, Color(0, 0, 0));
        }
        // Record indicator - this exact slot is this track's own current
        // recording target (Controller::isSessionRecording(track_id),
        // armed by "toggle-record-arm" or the per-track Record Arm -
        // several tracks can each have their own now, unlike a shared
        // single target), whether it was empty a moment ago or already
        // held a clip ("overwrite in place" -
        // Controller::beginSampleCapture()'s own comment) - takes priority
        // over either of those. Plain, common Unicode (U+25CF, unlike the
        // loop glyph's own ambiguous-width caution above), so no extra
        // width slack is needed for it.
        if (getController().isSessionRecording(track_id) &&
            static_cast<int>(clip_row) == getController().getSessionRecordingClipIndex(track_id)) {
          text = " ●";
          row_fg = Color(255, 60, 60);
        }
        if (is_cursor_cell) {
          // The plain green highlight_bg_color reads poorly here - a
          // clip row's own background is already a track identity color,
          // sometimes itself green - so the cursor brightens that same
          // color toward white instead of overriding it with an unrelated
          // one, the same "brighten the real color rather than replace
          // it" convention ArrangementGrid's own cursor cell uses for its
          // colored instance cells.
          row_bg = row_bg.blend(0.35f, kWhite);
        }
        setFgColor(row_fg);
        setBgColor(row_bg);
        putstr(y, x, string(static_cast<size_t>(kColWidth), ' ')); // opaque row background first
        putstr(y, x, text);
        if (has_real_clip && clips[clip_row].isLooping()) putstr(y, x + kColWidth - 2, "↻");
        continue;
      }

      string text;
      Color fg = styles.window_fg_color, bg = styles.window_bg_color;
      bool is_divider = physical_row == 8 || physical_row == 11;

      if (is_divider) {
        text = string(static_cast<size_t>(kColWidth), '-');
        fg = styles.window_border_color;
      } else if (physical_row == 9) { // Sends label (decorative, not cursor-addressable) - "Sends" itself lives here, its own unit ("dB") on the value row right under it
        text = fmt::format("{:<6}{:>4}{:>4}{:>4}", "Sends", "M", "A", "B");
        fg = styles.window_accent_fg_color;
      } else if (physical_row == 12) { // Direction label (decorative)
        text = fmt::format("{:>6}{:>6}{:>6}", "Az", "El", "Dist");
        fg = styles.window_accent_fg_color;
      } else if (physical_row == 13 && leaf) { // Direction value, azimuth/elevation/distance on one line
        text = fmt::format("{:>6.0f}{:>6.0f}{:>6.1f}", leaf->getAzimuth(), leaf->getElevation(), leaf->getDistance());
      }
      text = Utf8::padToWidth(Utf8::truncateToWidth(text, kColWidth), kColWidth);

      // Only the Direction value row (13) ever reaches here with
      // is_cursor_cell true - the Sends value row and every clip row are
      // handled directly above, each with their own cursor treatment.
      if (is_cursor_cell) {
        fg = styles.highlight_fg_color;
        bg = kBrightGrey;
      }
      setFgColor(fg);
      setBgColor(bg);
      putstr(y, x, text);
    }

    setFgColor(styles.window_border_color);
    setBgColor(styles.window_accent_bg_color); // matches the header row's own dark grey backdrop, not the plain window background below it
    putstr(0, x + kColWidth, "│");
    setBgColor(styles.window_bg_color);
    for (auto y = 1; y < rows; y++) putstr(y, x + kColWidth, "│");
  }

  return true;
}
