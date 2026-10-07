#include "ClipGrid.h"
#include "LevelMeter.h"
#include "../ScenePatternSource.h"

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
#include <chrono>
#include <cmath>
#include <memory>
#include <fmt/core.h>

using namespace std;

namespace {

const LeafTrack *
asLeafTrack(const Song & song, int track_id) {
  return dynamic_cast<const LeafTrack *>(song.getMasterTrack().getChildByInternalId(track_id));
}

// Where a clip slot's background sits between its dark and bright state
// color right now (0..1): playing and recording pulse smoothly, queued
// states flash on and off - the terminal's stand-in for the Launchpad's own
// hardware-animated pulse/flash.
float
statePulse(ClipHighlight state) {
  using Clock = std::chrono::steady_clock;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  switch (state) {
  case ClipHighlight::PLAYING:
  case ClipHighlight::RECORDING: {
    float phase = static_cast<float>(ms % 1000) / 1000.0f;
    return 0.5f - 0.5f * std::cos(phase * 6.2831853f);
  }
  case ClipHighlight::QUEUED:
  case ClipHighlight::RECORD_QUEUED:
  case ClipHighlight::RECORD_STOPPING:
    return (ms / 250) % 2 == 0 ? 1.0f : 0.0f;
  default: return 0.0f;
  }
}

// A send's linear level in dB, clamped so a 3-character column (sign + 2
// digits) always fits a real number rather than needing "-inf"/a wider
// column - -99 reads as "effectively off" just as clearly as the true
// floor would.
float sendDb(float linear) { return std::max(sendLinearToDb(linear), -99.0f); }

// A Sends label/value row's text, kSendsTextWidth wide (the column's last
// cell is the meter's): the same for a track and the master.
std::string sendsLabel() { return fmt::format("{:<5}{:>4}{:>4}{:>4}", "Sends", "M", "A", "B"); }
std::string sendValues(const SendLevels & sends) {
  return fmt::format("{:>4.0f}{:>4.0f}{:>4.0f}", sendDb(sends.main), sendDb(sends.a), sendDb(sends.b));
}

// A header's trailing " ◆IMS": taken over by Live View (a double-width
// glyph), Monitor, Mute, Solo.
constexpr int kHeaderFlagsWidth = 5;

}

ClipGrid::ClipGrid(UIPlane & parent) : UIElement(parent) {
  // Mute/Solo apply to the cursor's own column (a per-track state, not
  // tied to any particular row), same as the 'l' loop toggle stays a
  // manual offerInput() branch below rather than a command - there's
  // nothing else in this app either binding is likely to collide with.
  commands_.define("rename-track", [this]() {
    const Song & song = getController().getSong();
    startTrackRename(song, song.getPlayableTrackIds());
  });
  commands_.define("toggle-mute", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    bool now_muted = getController().toggleTrackMuted(track_id);
    getController().getUIEventQueue().push(std::make_unique<LogEvent>(now_muted ? "Muted" : "Unmuted"));
  });
  commands_.define("cycle-monitor", [this]() {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    getController().cycleTrackMonitor(track_ids[static_cast<size_t>(cursor_track_index_)]);
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
  keymap_.bind(KeyChord::pack('i', false, false, false, false), "cycle-monitor");
  keymap_.bind(KeyChord::pack('\\', false, false, false, false), "toggle-mute");
  keymap_.bind(KeyChord::pack('\\', true, false, false, false), "toggle-solo");

  // The slot under the cursor, when it's a clip row on a real track.
  auto cursor_slot = [this]() -> std::optional<std::pair<int, int>> {
    if (rowKindFor(cursor_row_) != RowKind::CLIP) return std::nullopt;
    auto track_ids = getController().getSong().getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return std::nullopt;
    // A CLIP row's own physical offset doubles as its clip-list index.
    return std::make_pair(track_ids[static_cast<size_t>(cursor_track_index_)], physicalFor(cursor_row_));
  };
  auto copy_clip = [this, cursor_slot]() {
    auto slot = cursor_slot();
    if (!slot) return false;
    auto & song = getController().getSong();
    auto & clips = song.getClips(slot->first);
    if (slot->second < 0 || slot->second >= static_cast<int>(clips.size()) || clips[static_cast<size_t>(slot->second)].isEmpty()) return false;
    clip_clipboard_ = clips[static_cast<size_t>(slot->second)];
    auto track = song.getMasterTrack().getChildByInternalId(slot->first);
    clip_clipboard_track_type_ = track ? static_cast<int>(track->getType()) : -1;
    return true;
  };
  // Copies the clip under the cursor into the clipboard.
  commands_.define("kill-ring-save", [this, copy_clip]() {
    getController().getUIEventQueue().push(std::make_unique<LogEvent>(copy_clip() ? "Copied clip" : "No clip to copy"));
  });
  // Cuts the clip under the cursor into the clipboard and removes it from
  // its slot; on an empty slot it removes the slot's stop button instead,
  // which isn't clip content and never reaches the clipboard. Del,
  // Backspace and Ctrl-K are bound too - the same keys this app treats as
  // "remove what's at the cursor" elsewhere.
  commands_.define("kill-region", [this, cursor_slot, copy_clip]() {
    auto slot = cursor_slot();
    if (!slot) return;
    copy_clip();
    getController().getClipPlayer().deleteClip(slot->first, slot->second);
  });
  // Pastes the clipboard's clip into the slot under the cursor as an
  // independent copy, overwriting what is there. Copying a clip and
  // yanking it onto the slot below is how a clip is duplicated.
  commands_.define("yank", [this, cursor_slot]() {
    auto slot = cursor_slot();
    if (!slot) return;
    auto & log = getController().getUIEventQueue();
    if (!clip_clipboard_) {
      log.push(std::make_unique<LogEvent>("Nothing to yank"));
      return;
    }
    auto & song = getController().getSong();
    auto track = song.getMasterTrack().getChildByInternalId(slot->first);
    if (!track || static_cast<int>(track->getType()) != clip_clipboard_track_type_) {
      log.push(std::make_unique<LogEvent>("Yank: that clip came from a different kind of track"));
      return;
    }
    placeClipCopy(song, slot->first, slot->second, *clip_clipboard_);
  });
  // Snaps the notes of the clip under the cursor to the nearest row.
  commands_.define("quantize-clip", [this]() {
    if (rowKindFor(cursor_row_) != RowKind::CLIP) return;
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    auto done = quantizeClip(song, track_ids[static_cast<size_t>(cursor_track_index_)], physicalFor(cursor_row_));
    getController().getUIEventQueue().push(std::make_unique<LogEvent>(done ? "Quantised clip" : "Quantise: nothing to quantise there"));
  });
  // Adds or removes the stop button of the empty slot under the cursor -
  // a no-op on a slot with a clip, same as kill-region on an empty one.
  commands_.define("toggle-stop-button", [this]() {
    if (rowKindFor(cursor_row_) != RowKind::CLIP) return;
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
    auto clip_row = physicalFor(cursor_row_);
    auto & clips = song.getClips(track_id);
    if (clip_row < 0 || (static_cast<size_t>(clip_row) < clips.size() && !clips[static_cast<size_t>(clip_row)].isEmpty())) return;
    auto & slot = song.ensureClipAt(track_id, clip_row);
    slot.setStopButton(!slot.hasStopButton());
    song.incVersion();
  });
  // Del, Backspace, and Ctrl-K all reach it - the same three keys this
  // app already treats as "delete something at the cursor" elsewhere
  // (Backspace/Del clearing note content, Ctrl-K placing a stop instance,
  // both in PatternEditor; ArrangementGrid's own Backspace doing the
  // same) - rather than a dedicated modifier chord of its own.
  keymap_.bind(KeyChord::pack(NCKEY_DEL, false, false, false, false), "kill-region");
  keymap_.bind(KeyChord::pack(NCKEY_BACKSPACE, false, false, false, false), "kill-region");
  keymap_.bind(KeyChord::pack('k', true, false, false, false), "kill-region");
  keymap_.bind(KeyChord::pack('w', true, false, false, false), "kill-region");
  keymap_.bind(KeyChord::pack('w', false, true, false, false), "kill-ring-save");
  keymap_.bind(KeyChord::pack('y', true, false, false, false), "yank");
  assertCommandBindingsValid();
}

int
ClipGrid::clipRowCount() const {
  return ScenePatternSource::sceneCount(getController().getSong());
}

ClipGrid::RowKind
ClipGrid::rowKindFor(int logical_row) const {
  if (logical_row == 0) return RowKind::HEADER;
  if (logical_row <= clipRowCount()) return RowKind::CLIP;
  if (logical_row == clipRowCount() + 1) return RowKind::SENDS;
  return RowKind::DIRECTION;
}

int
ClipGrid::physicalFor(int logical_row) const {
  auto clip_rows = clipRowCount();
  if (logical_row <= 0) return -1;
  if (logical_row <= clip_rows) return logical_row - 1;
  if (logical_row == clip_rows + 1) return clip_rows + kSendsValue;
  return clip_rows + kDirectionValue;
}

void
ClipGrid::ensureCursorVisible(int visible_rows, int visible_cols, int num_tracks) {
  // Column num_tracks is the master's, the last one.
  auto num_columns = num_tracks + 1;
  cursor_track_index_ = clamp(cursor_track_index_, 0, num_tracks);
  cursor_row_ = clamp(cursor_row_, 1, logicalRowCount() - 1); // never the header row

  scroll_col_ = clamp(scroll_col_, 0, max(0, num_columns - visible_cols));
  scroll_row_ = clamp(scroll_row_, 0, max(0, physicalRowCount() - visible_rows));
  if (view_detached_) return; // the mouse wheel scrolled away from the cursor

  if (cursor_track_index_ < scroll_col_) scroll_col_ = cursor_track_index_;
  if (visible_cols > 0 && cursor_track_index_ >= scroll_col_ + visible_cols) scroll_col_ = cursor_track_index_ - visible_cols + 1;
  scroll_col_ = clamp(scroll_col_, 0, max(0, num_columns - visible_cols));

  auto cursor_physical = physicalFor(cursor_row_);
  if (cursor_physical < scroll_row_) scroll_row_ = cursor_physical;
  if (visible_rows > 0 && cursor_physical >= scroll_row_ + visible_rows) scroll_row_ = cursor_physical - visible_rows + 1;
  scroll_row_ = clamp(scroll_row_, 0, max(0, physicalRowCount() - visible_rows));
}

void
ClipGrid::startClipRename(const Song & song, const std::vector<int> & track_ids) {
  if (inline_editor_.isOpen()) return;
  if (rowKindFor(cursor_row_) != RowKind::CLIP) return; // not on a clip row at all
  if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
  auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
  auto & clips = song.getClips(track_id);
  auto clip_row = physicalFor(cursor_row_); // a CLIP row's own physical offset doubles as its clip-list index
  // Same content-aware "an empty filler reads as no clip here" reasoning
  // as kill-region above - nothing to give a name to yet.
  if (clip_row < 0 || static_cast<size_t>(clip_row) >= clips.size() || clips[static_cast<size_t>(clip_row)].isEmpty()) return; // no clip here to rename

  auto physical_row = clip_row - scroll_row_ + 1; // +1 for the header row
  auto [ rows, cols ] = getDim();
  if (physical_row < 0 || physical_row >= rows) return; // off-screen - shouldn't happen given ensureCursorVisible(), a cosmetic nuisance if it ever does
  auto x = cursor_track_index_ - scroll_col_;
  if (x < 0) return; // column itself scrolled off - same defensive case
  auto col_x = x * (kColWidth + 1);

  InlineEditor::Field field;
  field.row = physical_row;
  field.col = col_x;
  field.width = kColWidth;
  field.initial_text = clips[static_cast<size_t>(clip_row)].getName();
  inline_editor_.open(field, [this, track_id, clip_row](std::string text) {
    auto & target_song = getController().getSong();
    auto & target_clips = target_song.getClips(track_id);
    if (static_cast<size_t>(clip_row) < target_clips.size()) {
      target_clips[static_cast<size_t>(clip_row)].setName(std::move(text));
      target_song.incVersion();
    }
  });
}

void ClipGrid::startSceneRename() {
  if (inline_editor_.isOpen()) return;
  auto scene = physicalFor(cursor_row_);
  auto row = scene - scroll_row_ + 1; // +1 for the header row
  auto column = cursor_track_index_ - scroll_col_;
  if (row < 1 || row >= getDim().first || column < 0) return;

  // The editable span follows the " ▸ " launch glyph. Typing "90 BPM" or
  // "3/4" sets the scene's tempo or time signature (shown at the right of
  // the slot), "0 BPM" or "0/4" clears it; a name without one leaves it alone.
  // The field is wider than the slot, since "Waltz 3/4 90 BPM" doesn't fit
  // in it and the reader keeps only what fits.
  constexpr int kSceneFieldWidth = 32;
  auto cols = getDim().second;
  auto width = min(kSceneFieldWidth, cols);
  InlineEditor::Field field;
  field.row = row;
  field.col = clamp(column * (kColWidth + 1) + 3, 0, max(0, cols - width));
  field.width = width;
  field.initial_text = getController().getSong().getSceneName(scene);
  inline_editor_.open(field, [this, scene](std::string text) {
    auto & target_song = getController().getSong();
    target_song.setSceneFromText(scene, text);
    target_song.incVersion();
  });
}

void
ClipGrid::startTrackRename(const Song & song, const std::vector<int> & track_ids) {
  if (inline_editor_.isOpen()) return;
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
  // trailing " ◆IMS" flags, matching render()'s own
  // header layout exactly so the reader lands right over the name it's
  // replacing.
  SongStructure structure(song);
  auto prefix = "T" + std::to_string(structure.getBaselineInfo(track_id).color_ordinal_) + " ";
  auto name_area_width = kColWidth - kHeaderFlagsWidth;
  auto prefix_width = std::min(static_cast<int>(prefix.size()), name_area_width);
  auto edit_col = col_x + prefix_width;
  auto edit_width = std::max(name_area_width - prefix_width, 1);

  InlineEditor::Field field;
  field.row = 0;
  field.col = edit_col;
  field.width = edit_width;
  field.initial_text = track->getName();
  inline_editor_.open(field, [this, track_id](std::string text) {
    auto & target_song = getController().getSong();
    if (auto * target = target_song.getMasterTrack().getChildByInternalId(track_id)) {
      target->setName(std::move(text));
      target_song.incVersion();
    }
  });
}

void
ClipGrid::activateCell(const Song & song, const std::vector<int> & track_ids, bool edit_sends) {
  auto num_tracks = static_cast<int>(track_ids.size());
  // A clip row acts exactly like a Launchpad Live View pad press
  // landing on that same cell (ClipPlayer::triggerClip(), via
  // trigger_callback_ - see setTriggerCallback()'s own comment) - an empty
  // row stops/cancels whatever the track is doing, the same as pressing an
  // unassigned pad would, so it's called unconditionally on any CLIP row
  // rather than only a populated one.
  auto kind = rowKindFor(cursor_row_);
  if (cursor_track_index_ == num_tracks) {
    // The master column: a clip row launches that scene, the Sends row
    // edits the master's Send Main (the song's volume), the last row
    // stops every track.
    if (kind == RowKind::CLIP && scene_callback_) scene_callback_(physicalFor(cursor_row_));
    else if (kind == RowKind::SENDS && edit_sends) startSendsEdit(song.getMasterTrack().getInternalId());
    else if (kind == RowKind::DIRECTION && stop_all_callback_) stop_all_callback_();
    return;
  }
  if (cursor_track_index_ < 0 || cursor_track_index_ > num_tracks) return;
  if (kind == RowKind::SENDS) {
    if (edit_sends) startSendsEdit(track_ids[static_cast<size_t>(cursor_track_index_)]);
  } else if (kind == RowKind::CLIP && trigger_callback_) {
    trigger_callback_(track_ids[static_cast<size_t>(cursor_track_index_)], physicalFor(cursor_row_));
  }
  // DIRECTION: read-only for now - nothing to do.
}

bool
ClipGrid::handleMouse(const InputEvent & input) {
  if (input.getKind() == InputEvent::Kind::RELEASE) {
    mouse_down_ = false;
    return true;
  }
  const Song & song = getController().getSong(); // see offerInput()'s own comment on why const
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());
  auto [ pos_y, pos_x ] = getPosition();
  auto [ rows, cols ] = getDim();
  auto y = input.getY() - pos_y, x = input.getX() - pos_x;
  if (y < 0 || y >= rows || x < 0 || x >= cols) return false;

  // A held button repeating its press as the mouse moves isn't a new click.
  bool fresh_press = !mouse_down_;
  mouse_down_ = true;

  auto column = scroll_col_ + x / (kColWidth + 1);
  if (x % (kColWidth + 1) == kColWidth || column > num_tracks) return true; // a divider, or past the last column

  auto clip_rows = clipRowCount();
  int logical_row = 0;
  if (y > 0) {
    auto physical = scroll_row_ + y - 1;
    if (physical < clip_rows) logical_row = physical + 1;
    else if (physical == clip_rows + kSendsValue) logical_row = clip_rows + 1;
    else if (physical == clip_rows + kDirectionValue) logical_row = clip_rows + 2;
  }

  cursor_track_index_ = column;
  if (logical_row == 0) return true; // the header, or a label/divider row: only the track is picked
  cursor_row_ = logical_row;
  view_detached_ = false;
  if (column < num_tracks) getController().getSong().setCurrentTrackId(track_ids[static_cast<size_t>(column)]); // non-const, see offerInput()'s own `song` comment

  if (fresh_press) activateCell(song, track_ids, false);
  return true;
}

bool
ClipGrid::offerInput(const InputEvent & input) {
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

  // While a rename editor is open it owns every key.
  if (inline_editor_.offerInput(input)) return true;

  if (input.getId() == NCKEY_BUTTON1) return handleMouse(input);

  // The mouse wheel scrolls the view (Shift: tracks), not the cursor, until
  // the cursor next moves.
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
    // Renames the clip under the cursor, or the track when there's no
    // clip there (the cursor never sits on the header row itself); on the
    // master column's scene slot, the scene.
    if (cursor_track_index_ == num_tracks && rowKindFor(cursor_row_) == RowKind::CLIP) {
      startSceneRename();
      return true;
    }
    auto clip_index = getCursorClipIndex();
    bool on_clip = false;
    if (clip_index >= 0 && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
      auto & clips = song.getClips(track_ids[static_cast<size_t>(cursor_track_index_)]);
      on_clip = clip_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(clip_index)].isEmpty();
    }
    if (on_clip) startClipRename(song, track_ids);
    else startTrackRename(song, track_ids);
    return true;
  } else if (input.getId() == 'l' && !input.hasCtrl() && !input.hasAlt()) {
    // Loop toggle - only meaningful on a clip row that actually has a
    // clip; a no-op (but still consumed) anywhere else, same "always
    // does something or nothing, never falls through" precedent
    // ArrangementGrid's own Enter handling already has.
    if (rowKindFor(cursor_row_) == RowKind::CLIP && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
      auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
      auto & clips = song.getClips(track_id);
      auto clip_row = physicalFor(cursor_row_); // a CLIP row's own physical offset doubles as its clip-list index
      if (clip_row >= 0 && static_cast<size_t>(clip_row) < clips.size() && !clips[static_cast<size_t>(clip_row)].isEmpty()) {
        auto & mutable_song = getController().getSong(); // non-const - this branch genuinely writes, unlike the rest of this method (see `song`'s own comment above)
        auto & clip = mutable_song.getClips(track_id)[static_cast<size_t>(clip_row)];
        clip.setLooping(!clip.isLooping());
        mutable_song.incVersion();
      }
    }
    return true;
  } else if (input.getId() == NCKEY_ENTER) {
    activateCell(song, track_ids, true);
    return true;
  }
  else return false;

  cursor_track_index_ = clamp(cursor_track_index_, 0, num_tracks); // num_tracks: the master column
  cursor_row_ = clamp(cursor_row_, 1, logicalRowCount() - 1); // never the header row

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
ClipGrid::render(const StyleProvider & styles, bool refresh, bool focused) {
  const Song & song = getController().getSong(); // see offerInput()'s own comment on why const
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());

  auto [ rows, cols ] = getDim();
  if (rows < 2 || cols < 1) return false;

  auto visible_rows = max(0, rows - 1); // row 0 is the header, never scrolled
  auto visible_cols = max(0, cols) / (kColWidth + 1); // whole columns, what the cursor is kept within
  // Columns drawn: a last one cut off by the edge too, so the whole width fills.
  auto drawn_cols = (max(0, cols) + kColWidth) / (kColWidth + 1);

  // A moved cursor reattaches a view the mouse wheel detached.
  if (view_detached_ && (cursor_track_index_ != current_cursor_track_index_ || cursor_row_ != current_cursor_row_)) view_detached_ = false;
  ensureCursorVisible(visible_rows, visible_cols, num_tracks);

  // Which clip (if any) is currently focused for editing - shown as a
  // marker on its own row below, independent of cursor position. Read
  // once here (not per-cell) since it's the same value for every column.
  auto focused_clip_id = getController().getFocusedClip();

  auto new_version = song.getMajorVersion();
  bool editor_redraw = inline_editor_.consumeRedrawRequest();
  auto clip_rows = clipRowCount();
  auto clipState = [&](int track_index, int clip_row) {
    if (!clip_state_source_ || track_index >= num_tracks || clip_row >= clip_rows) return ClipHighlight::NONE;
    return clip_state_source_(track_ids[static_cast<size_t>(track_index)], clip_row);
  };
  // A scene's state, for its master slot: queued while any track in it is
  // about to launch or record, playing while any plays or records.
  auto sceneState = [&](int clip_row) {
    auto state = ClipHighlight::NONE;
    for (int t = 0; t < num_tracks; t++) {
      auto s = clipState(t, clip_row);
      if (s == ClipHighlight::QUEUED || s == ClipHighlight::RECORD_QUEUED) return ClipHighlight::QUEUED;
      if (s == ClipHighlight::PLAYING || s == ClipHighlight::RECORDING) state = ClipHighlight::PLAYING;
      if (s == ClipHighlight::PAUSED && state == ClipHighlight::NONE) state = ClipHighlight::PAUSED;
    }
    return state;
  };
  auto master_id = song.getMasterTrack().getInternalId();
  // The track (or, past the last track, the master) a column index shows.
  auto columnTrackId = [&](int column) { return column < num_tracks ? track_ids[static_cast<size_t>(column)] : master_id; };
  std::vector<ClipHighlight> clip_states;
  for (auto vc = 0; vc < drawn_cols && scroll_col_ + vc <= num_tracks; vc++) {
    auto column = scroll_col_ + vc;
    for (auto vr = 0; vr < visible_rows; vr++) {
      clip_states.push_back(column < num_tracks ? clipState(column, scroll_row_ + vr) : sceneState(scroll_row_ + vr));
    }
  }
  std::vector<int> track_clips;
  for (auto track_id : track_ids) track_clips.push_back(track_clip_source_ ? track_clip_source_(track_id) : -1);
  bool clip_states_changed = clip_states != current_clip_states_ || track_clips != current_track_clips_;
  current_clip_states_ = std::move(clip_states);
  current_track_clips_ = std::move(track_clips);

  // The meters, in bar steps (negative while clipping) - only a visible
  // change redraws.
  auto & playback_info = getController().getPlaybackInfo();
  auto now = std::chrono::steady_clock::now();
  auto meter_dt = std::min(std::chrono::duration<float>(now - last_meter_update_).count(), 0.25f);
  last_meter_update_ = now;
  std::vector<int> meter_steps;
  for (auto vc = 0; vc < drawn_cols && scroll_col_ + vc <= num_tracks; vc++) {
    auto track_id = columnTrackId(scroll_col_ + vc);
    auto & track_info = playback_info.getTrackInfo(track_id);
    auto & meter = meters_[track_id];
    meter.fraction = level_meter::fraction(meter.ballistics.update(track_info.getMeterValue(), meter_dt));
    meter.peak_fraction = meter.peak_hold.update(meter.fraction, meter_dt);
    auto steps = level_meter::barSteps(meter.fraction, kMeterRows);
    meter_steps.push_back(track_info.isClipping() ? -1 - steps : steps);
    meter_steps.push_back(level_meter::barSteps(meter.peak_fraction, kMeterRows));
  }
  bool meters_changed = meter_steps != current_meter_steps_;
  current_meter_steps_ = std::move(meter_steps);

  if (!refresh && !editor_redraw && !clip_states_changed && !meters_changed && new_version == current_song_version_ &&
      cursor_track_index_ == current_cursor_track_index_ && cursor_row_ == current_cursor_row_ &&
      scroll_col_ == current_scroll_col_ && scroll_row_ == current_scroll_row_ &&
      focused == current_focused_ && focused_clip_id == current_focused_clip_id_) {
    return false;
  }
  current_focused_clip_id_ = focused_clip_id;
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
  auto cursor_physical = physicalFor(cursor_row_);

  for (auto vc = 0; vc < drawn_cols; vc++) {
    auto track_index = scroll_col_ + vc;
    if (track_index == num_tracks) {
      auto & master_info = playback_info.getTrackInfo(master_id);
      renderMasterColumn(styles, vc * (kColWidth + 1), rows, focused, num_tracks, sceneState);
      renderMeter(styles, vc * (kColWidth + 1), rows, master_id, master_info.isClipping());
      break;
    }
    if (track_index > num_tracks) break;
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
    auto name_width = kColWidth - kHeaderFlagsWidth;
    // -1 is the header row's own physical value (physicalFor()'s own
    // comment) - never a real physical_row inside the loop below, so this
    // is the header's one and only cursor check.
    bool header_is_cursor = focused && track_index == cursor_track_index_ && cursor_physical == -1;
    setFgColor(header_is_cursor ? styles.highlight_fg_color : styles.window_accent_fg_color);
    setBgColor(header_is_cursor ? styles.highlight_bg_color : styles.heading_bg_color);
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
    auto monitor = leaf ? leaf->getMonitor() : LeafTrack::Monitor::AUTO;
    // The leading two cells mark a track playing Live View rather than
    // the arrangement; otherwise they're left to the header's own blank.
    if (getController().getClipPlayer().isTakenOver(track_id)) {
      setFgColor(styles.clip_override_color);
      putstr(0, x + name_width, "◆");
    }
    setFgColor(monitor == LeafTrack::Monitor::IN ? styles.monitor_color :
               monitor == LeafTrack::Monitor::AUTO ? styles.window_fg_color : styles.window_border_color);
    putstr(0, x + name_width + 2, monitor == LeafTrack::Monitor::AUTO ? "A" : "I");
    setFgColor(muted ? styles.mute_color : styles.window_border_color);
    putstr(0, x + name_width + 3, "M");
    setFgColor(solo ? styles.solo_color : styles.window_border_color);
    putstr(0, x + name_width + 4, "S");

    for (auto vr = 0; vr < visible_rows; vr++) {
      auto physical_row = scroll_row_ + vr;
      auto y = 1 + vr;
      bool is_cursor_cell = focused && track_index == cursor_track_index_ && physical_row == cursor_physical;
      // The cursor track's clip being edited in the pattern editor below
      // is marked with a pencil.
      bool is_edited_clip = track_clip_source_ && track_index == cursor_track_index_ && physical_row < clip_rows && physical_row == track_clip_source_(track_id);

      // The Sends value row mixes a cursive unit label with plain-weight
      // numbers, which a single putstr call can't do - handled directly
      // here (two calls, two styles) rather than through the shared
      // text/pad/put path every other row below shares.
      if (physical_row == clip_rows + kSendsValue) {
        Color row_fg = is_cursor_cell ? styles.highlight_fg_color : styles.window_fg_color;
        Color row_bg = is_cursor_cell ? styles.highlight_bg_color : styles.window_bg_color;
        setFgColor(row_fg);
        setBgColor(row_bg);
        putstr(y, x, string(static_cast<size_t>(kColWidth), ' ')); // opaque row background first
        if (leaf) {
          auto sends = leaf->getSends();
          setItalic(true);
          putstr(y, x, " dB  "); // under "Sends" above it
          setItalic(false);
          putstr(y, x + 5, sendValues(sends));
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
      if (physical_row < clip_rows) {
        auto clip_row = static_cast<size_t>(physical_row);
        Color row_fg = styles.clip_text_color, row_bg = styles.window_bg_color;
        string text;
        // Content-aware, not just in-bounds - holes are allowed (Song::
        // ensureClipAt()), so an in-bounds but still-empty filler renders
        // as the plain "nothing here" stop icon below, same as a row
        // genuinely past the list's own end.
        bool has_real_clip = clip_row < clips.size() && !clips[clip_row].isEmpty();
        if (has_real_clip) {
          auto & clip = clips[clip_row];
          // The number as the pattern editor counts clips, then the name if it has one.
          auto name = std::to_string(clip_row + 1);
          if (!clip.getName().empty()) name += " " + clip.getName();
          // Leading marker: whether this clip is the one currently
          // focused for editing (Controller::getFocusedClip(), set when
          // a Launchpad opens it for step editing), a blank space otherwise so the play glyph/
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
          // Icons pack against the right edge (loop rightmost), two columns
          // each: they may render two cells wide.
          auto trailing = std::max(2, 2 * ((is_edited_clip ? 1 : 0) + (clip.isLooping() ? 1 : 0)));
          auto clip_name_width = kColWidth - 3 - trailing; // marker + "▸ " + the trailing columns
          auto name_field = Utf8::padToWidth(Utf8::truncateToWidth(name, clip_name_width), clip_name_width);
          text = fmt::format("{}▸ {}", marker, name_field);
          row_bg = structure.getBaselineInfo(track_id).getColor();
        } else {
          // Without a stop button, launching the slot does nothing.
          bool stops = clip_row >= clips.size() || clips[clip_row].hasStopButton();
          text = stops ? " ⏹" : "  ";
          // Dim - an empty slot is background information, not something
          // to draw the eye the way a real clip's own bright white does -
          // but not window_border_color's own near-invisible divider
          // shade either, halfway to plain text gray instead so the glyph
          // still reads clearly as a real stop icon, not a smudge.
          row_fg = styles.window_fg_color.blend(0.5f, Color(0, 0, 0));
        }
        // The slot's transport/recording state, as its Launchpad pad shows
        // it (ClipHighlight): a colored glyph in the icon's place -
        // green for a clip playing or queued to launch (dim while the
        // transport is paused), red for an armed
        // track's slots (dim while merely armed or stopping, bright while
        // a take is queued or recording). The glyph's shape tells queued
        // from running; the slot's background below also pulses/flashes.
        auto state = clipState(track_index, static_cast<int>(clip_row));
        const char * glyph = nullptr;
        Color glyph_fg = row_fg;
        switch (state) {
        case ClipHighlight::NONE: break;
        case ClipHighlight::PLAYING: glyph = "▸"; glyph_fg = styles.clip_playing_color; break;
        case ClipHighlight::PAUSED: glyph = "▸"; glyph_fg = styles.clip_paused_color; break;
        case ClipHighlight::QUEUED: glyph = "▹"; glyph_fg = styles.clip_playing_color; break;
        case ClipHighlight::ARMED_EMPTY: glyph = "○"; glyph_fg = styles.clip_armed_color; break;
        case ClipHighlight::RECORD_QUEUED: glyph = "○"; glyph_fg = styles.clip_recording_color; break;
        case ClipHighlight::RECORDING: glyph = "●"; glyph_fg = styles.clip_recording_color; break;
        case ClipHighlight::RECORD_STOPPING: glyph = "●"; glyph_fg = styles.clip_armed_color; break;
        }
        // The slot takes the state color, pulsing or flashing between its
        // dark and bright shade like the Launchpad pad (before the cursor/
        // tint below, so those still apply on top).
        if (has_real_clip && state != ClipHighlight::NONE) {
          bool red = state == ClipHighlight::RECORDING || state == ClipHighlight::RECORD_QUEUED
            || state == ClipHighlight::RECORD_STOPPING;
          Color bright = red ? styles.clip_recording_color : styles.clip_playing_color;
          Color dark = bright.blend(0.6f, Color(0, 0, 0));
          row_bg = state == ClipHighlight::PAUSED ? styles.clip_paused_color
            : dark.blend(statePulse(state), bright);
        }
        if (is_cursor_cell && !has_real_clip) {
          row_fg = styles.highlight_fg_color;
          row_bg = styles.highlight_bg_color;
        } else if (is_cursor_cell) {
          // A clip's background is its track's identity color - the cursor
          // brightens it rather than replacing it, the same as
          // ArrangementGrid's cursor on its colored instance cells.
          row_fg = styles.highlight_fg_color;
          row_bg = row_bg.blend(0.5f, styles.cursor_tint_color);
        }
        setFgColor(row_fg);
        setBgColor(row_bg);
        putstr(y, x, string(static_cast<size_t>(kColWidth), ' ')); // opaque row background first
        putstr(y, x, text);
        if (has_real_clip && clips[clip_row].isLooping()) putstr(y, x + kColWidth - 2, "↻");
        if (is_edited_clip) putstr(y, x + kColWidth - (has_real_clip && clips[clip_row].isLooping() ? 4 : 2), "✎");
        if (glyph) {
          setFgColor(glyph_fg);
          putstr(y, x + 1, glyph);
        }
        continue;
      }

      string text;
      Color fg = styles.window_fg_color, bg = styles.window_bg_color;
      bool is_divider = physical_row == clip_rows + kSendsDivider || physical_row == clip_rows + kDirectionDivider;

      if (is_divider) {
        text = string(static_cast<size_t>(kColWidth), '-');
        fg = styles.window_border_color;
      } else if (physical_row == clip_rows + kSendsLabel) { // Sends label (decorative, not cursor-addressable) - "Sends" itself lives here, its own unit ("dB") on the value row right under it
        text = sendsLabel();
        fg = styles.window_accent_fg_color;
      } else if (physical_row == clip_rows + kDirectionLabel) { // Direction label (decorative)
        text = fmt::format("{:>5}{:>6}{:>6}", "Az", "El", "Dist");
        fg = styles.window_accent_fg_color;
      } else if (physical_row == clip_rows + kDirectionValue && leaf) { // Direction value, azimuth/elevation/distance on one line
        text = fmt::format("{:>5.0f}{:>6.0f}{:>6.1f}", leaf->getAzimuth(), leaf->getElevation(), leaf->getDistance());
      }
      text = Utf8::padToWidth(Utf8::truncateToWidth(text, kColWidth), kColWidth);

      // Only the Direction value row (13) ever reaches here with
      // is_cursor_cell true - the Sends value row and every clip row are
      // handled directly above, each with their own cursor treatment.
      if (is_cursor_cell) {
        fg = styles.highlight_fg_color;
        bg = styles.highlight_bg_color;
      }
      setFgColor(fg);
      setBgColor(bg);
      putstr(y, x, text);
    }

    auto & track_info = playback_info.getTrackInfo(track_id);
    renderMeter(styles, x, rows, track_id, track_info.isClipping());

    setFgColor(styles.window_border_color);
    setBgColor(styles.heading_bg_color); // the header row's own backdrop, not the plain window background below it
    putstr(0, x + kColWidth, "│");
    // Dividers belong to no track, so no track's clip mark reaches them.
    setBgColor(styles.window_bg_color);
    for (auto y = 1; y < rows; y++) putstr(y, x + kColWidth, "│");
  }

  inline_editor_.paintBackdrop();
  return true;
}

void
ClipGrid::renderMeter(const StyleProvider & styles, int x, int rows, int track_id, bool clipping) {
  auto & meter = meters_[track_id];
  auto cells = level_meter::verticalBar(meter.fraction, kMeterRows, meter.peak_fraction);
  auto top = clipRowCount() + kSendsLabel; // physical row of the meter's top cell
  setBgColor(styles.window_bg_color);
  for (int i = 0; i < kMeterRows; i++) {
    auto y = top + i - scroll_row_ + 1; // +1 for the header row
    // Shaded by the height of this cell; i = 0 is the top one.
    setFgColor(clipping ? styles.meter_clip_color : styles.meterColor((static_cast<float>(kMeterRows - i) - 0.5f) / static_cast<float>(kMeterRows)));
    if (y >= 1 && y < rows) putstr(y, x + kColWidth - 1, cells[static_cast<size_t>(i)]);
  }
}

void
ClipGrid::renderMasterColumn(const StyleProvider & styles, int x, int rows, bool focused, int num_tracks,
                             const std::function<ClipHighlight(int clip_row)> & scene_state) {
  const Song & song = getController().getSong();
  auto & master = song.getMasterTrack();
  auto clip_rows = clipRowCount();
  auto cursor_physical = physicalFor(cursor_row_);
  bool cursor_here = cursor_track_index_ == num_tracks;
  auto track_ids = song.getPlayableTrackIds();

  // Header: the master has no Mute/Solo or Monitor - only leaf tracks do.
  setFgColor(styles.window_accent_fg_color);
  setBgColor(styles.heading_bg_color);
  putstr(0, x, Utf8::padToWidth("Master", kColWidth));

  for (auto vr = 0; vr + 1 < rows; vr++) {
    auto physical_row = scroll_row_ + vr;
    auto y = 1 + vr;
    if (physical_row >= physicalRowCount()) break;
    bool is_cursor_cell = focused && cursor_here && physical_row == cursor_physical;
    Color fg = styles.window_fg_color, bg = styles.window_bg_color;
    if (is_cursor_cell) {
      fg = styles.highlight_fg_color;
      bg = styles.highlight_bg_color;
    }

    if (physical_row < clip_rows) {
      // A scene slot: launches clip row k on every track, showing the
      // scene's name after the launch glyph; dim when no track has a clip
      // there (launching it just stops everything).
      bool has_any_clip = false;
      for (auto track_id : track_ids) {
        auto & clips = song.getClips(track_id);
        if (static_cast<size_t>(physical_row) < clips.size() && !clips[static_cast<size_t>(physical_row)].isEmpty()) has_any_clip = true;
      }
      if (!has_any_clip && !is_cursor_cell) fg = styles.window_fg_color.blend(0.5f, Color(0, 0, 0));
      setFgColor(fg);
      setBgColor(bg);
      auto tempo = song.getSceneTempo(physical_row);
      auto signature = song.getSceneTimeSignature(physical_row);
      auto tempo_text = tempo > 0 ? " ♩" + std::to_string(tempo) : std::string();
      if (signature.isSet()) tempo_text += " " + std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
      auto name_width = kColWidth - 3 - Utf8::displayWidth(tempo_text);
      putstr(y, x, Utf8::padToWidth(" ▸ " + Utf8::padToWidth(Utf8::truncateToWidth(song.getSceneName(physical_row), name_width), name_width) + tempo_text, kColWidth));
      auto state = scene_state(physical_row);
      if (state == ClipHighlight::PLAYING || state == ClipHighlight::QUEUED || state == ClipHighlight::PAUSED) {
        setFgColor(state == ClipHighlight::PAUSED ? styles.clip_paused_color : styles.clip_playing_color);
        putstr(y, x + 1, state == ClipHighlight::QUEUED ? "▹" : "▸");
      }
      continue;
    }

    auto offset = physical_row - clip_rows;
    if (offset == kSendsValue) {
      // The master's own Send Main/A/B: the dry mix and the send bus's two
      // returns - the same row, and parameters, a track has.
      setFgColor(fg);
      setBgColor(bg);
      putstr(y, x, std::string(static_cast<size_t>(kColWidth), ' '));
      setItalic(true);
      putstr(y, x, " dB  ");
      setItalic(false);
      putstr(y, x + 5, sendValues(master.getSends()));
      continue;
    }
    std::string text;
    if (offset == kSendsDivider || offset == kDirectionDivider) {
      text = std::string(static_cast<size_t>(kColWidth), '-');
      fg = styles.window_border_color;
    } else if (offset == kSendsLabel) {
      text = sendsLabel();
      fg = styles.window_accent_fg_color;
    } else if (offset == kDirectionValue) {
      // The master has no position; its second addressable row stops
      // every track instead.
      text = " ⏹ Stop all";
    }
    setFgColor(fg);
    setBgColor(bg);
    putstr(y, x, Utf8::padToWidth(text, kColWidth));
  }

  // The trailing divider every column has, the header's in its backdrop.
  setFgColor(styles.window_border_color);
  setBgColor(styles.heading_bg_color);
  putstr(0, x + kColWidth, "│");
  setBgColor(styles.window_bg_color);
  for (auto y = 1; y < rows && scroll_row_ + y - 1 < physicalRowCount(); y++) putstr(y, x + kColWidth, "│");
}

void
ClipGrid::startSendsEdit(int track_id) {
  if (inline_editor_.isOpen()) return;
  auto row = clipRowCount() + kSendsValue - scroll_row_ + 1; // +1 for the header row
  auto column = cursor_track_index_ - scroll_col_;
  if (row < 1 || row >= getDim().first || column < 0) return;
  auto * track = getController().getSong().getMasterTrack().getChildByInternalId(track_id);
  if (!track) return;

  // The three values as shown, space-separated; each one typed replaces
  // its send, in order - M, A, B.
  InlineEditor::Field field;
  field.row = row;
  field.col = column * (kColWidth + 1) + 5;
  field.width = kSendsTextWidth - 5;
  auto sends = track->getSends();
  field.initial_text = fmt::format("{:.0f} {:.0f} {:.0f}", sendDb(sends.main), sendDb(sends.a), sendDb(sends.b));
  inline_editor_.open(field, [this, track_id](std::string text) {
    auto & controller = getController();
    const char * p = text.c_str();
    for (int i = 0; i < 3; i++) {
      char * end = nullptr;
      float db = std::strtof(p, &end);
      if (end == p) break; // no more numbers - the rest stay as they were
      db = std::clamp(db, -100.0f, 12.0f);
      if (i == 0) controller.setTrackSendMain(track_id, db);
      else if (i == 1) controller.setTrackSendA(track_id, db);
      else controller.setTrackSendB(track_id, db);
      p = end;
    }
  });
}
