#include "ClipGrid.h"
#include "LevelMeter.h"
#include "ScenePatternSource.h"

#include "../playback/InputEvent.h"
#include "KeyStroke.h"
#include "StyleProvider.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/ArrangementOps.h"
#include "../model/LeafTrack.h"
#include "../model/Clip.h"
#include "../model/SendFader.h"
#include "../model/SongStructure.h"
#include "../util/Utf8.h"

#include <cellui/Dial.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <fmt/format.h>

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
float statePulse(ClipHighlight state, bool reduced_motion) {
  // With motion off the slot holds a steady shade: full for running, half for
  // queued, whose glyph already tells them apart.
  if (reduced_motion) {
    switch (state) {
      case ClipHighlight::PLAYING:
      case ClipHighlight::RECORDING:
        return 1.0f;
      case ClipHighlight::QUEUED:
      case ClipHighlight::RECORD_QUEUED:
      case ClipHighlight::RECORD_STOPPING:
        return 0.5f;
      default:
        return 0.0f;
    }
  }
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

// A header's trailing " ◆IMS": taken over by Live View (a double-width
// glyph), Monitor, Mute, Solo.
constexpr int kHeaderFlagsWidth = 5;
}

ClipGrid::ClipGrid(Plane & parent) : UIElement(parent) {
  getPlane().setName("clip-grid");
  getPlane().setMouseCursor(cellui::MouseCursor::POINTER);
  // Mute/Solo apply to the cursor's own column (a per-track state, not
  // tied to any particular row), same as the 'l' loop toggle stays a
  // manual offerInput() branch below rather than a command - there's
  // nothing else in this app either binding is likely to collide with.
  commands_.define("rename", [this]() { startRename(); });
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
    getController().log(now_muted ? "Muted" : "Unmuted");
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
    getController().log(now_solo ? "Solo on" : "Solo off");
  });
  // 'm'/'s' are this view's own shortcuts; backslash/Ctrl-backslash match
  // PatternEditor's own toggle-mute/toggle-solo bindings, so either works
  // regardless of which of the two views is currently focused.
  keymap_.bind(KeyStroke::pack('m', false, false, false, false), "toggle-mute");
  keymap_.bind(KeyStroke::pack('s', false, false, false, false), "toggle-solo");
  keymap_.bind(KeyStroke::pack('i', false, false, false, false), "cycle-monitor");
  keymap_.bind(KeyStroke::pack('\\', false, false, false, false), "toggle-mute");
  keymap_.bind(KeyStroke::pack('\\', true, false, false, false), "toggle-solo");

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
    auto clips = song.getClips(slot->first);
    if (slot->second < 0 || slot->second >= static_cast<int>(clips.size()) || clips[static_cast<size_t>(slot->second)].isEmpty()) return false;
    clip_clipboard_ = clips[static_cast<size_t>(slot->second)].toClip();
    auto track = song.getMasterTrack().getChildByInternalId(slot->first);
    clip_clipboard_track_type_ = track ? static_cast<int>(track->getType()) : -1;
    return true;
  };
  // Copies the clip under the cursor into the clipboard.
  commands_.define("kill-ring-save", [this, copy_clip]() {
    getController().log(copy_clip() ? "Copied clip" : "No clip to copy");
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
    if (!clip_clipboard_) {
      getController().log("Nothing to yank");
      return;
    }
    auto & song = getController().getSong();
    auto track = song.getMasterTrack().getChildByInternalId(slot->first);
    if (!track || static_cast<int>(track->getType()) != clip_clipboard_track_type_) {
      getController().log("Yank: that clip came from a different kind of track");
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
    getController().log(done ? "Quantised clip" : "Quantise: nothing to quantise there");
  });
  // Stops the cursor track's clips on the next bar, leaving the other tracks.
  commands_.define("stop-track-clips", [this]() {
    auto track_ids = getController().getSong().getPlayableTrackIds();
    if (cursor_track_index_ < 0 || cursor_track_index_ >= static_cast<int>(track_ids.size())) return;
    getController().getClipPlayer().stopTrack(track_ids[static_cast<size_t>(cursor_track_index_)]);
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
    auto clips = song.getClips(track_id);
    if (clip_row < 0 || (static_cast<size_t>(clip_row) < clips.size() && !clips[static_cast<size_t>(clip_row)].isEmpty())) return;
    Song::Edit edit(song, "toggle stop button");
    auto slot = song.ensureClipAt(track_id, clip_row);
    slot.setStopButton(!slot.hasStopButton());
  });
  // Del, Backspace, and Ctrl-K all reach it - the same three keys this
  // app already treats as "delete something at the cursor" elsewhere
  // (Backspace/Del clearing note content, Ctrl-K placing a stop instance,
  // both in PatternEditor; ArrangementGrid's own Backspace doing the
  // same) - rather than a dedicated modifier shortcut of its own.
  keymap_.bind(KeyStroke::pack(Key::Delete, false, false, false, false), "kill-region");
  keymap_.bind(KeyStroke::pack(Key::Backspace, false, false, false, false), "kill-region");
  keymap_.bind(KeyStroke::pack('k', true, false, false, false), "kill-region");
  // Cut, copy and paste: the terminal keymap's shortcuts, the usual
  // desktop ones elsewhere.
  keymap_.bind(KeymapProfile::TERMINAL, KeyStroke::pack('w', true, false, false, false), "kill-region");
  keymap_.bind(KeymapProfile::TERMINAL, KeyStroke::pack('w', false, true, false, false), "kill-ring-save");
  keymap_.bind(KeymapProfile::TERMINAL, KeyStroke::pack('y', true, false, false, false), "yank");
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack('x', true, false, false, false), "kill-region");
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack('c', true, false, false, false), "kill-ring-save");
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack('v', true, false, false, false), "yank");
  // The terminal shortcut is InteractiveUI's: its C-c prefix is global, so a widget's
  // own C-c sequence would never be reached.
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack('r', true, false, false, false), "rename");
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack('u', true, false, false, false), "quantize-clip");
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack('e', true, false, false, false), "toggle-stop-button");
  keymap_.bind(KeymapProfile::DESKTOP, KeyStroke::pack(Key::Enter, true, false, false, false), "stop-track-clips");
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
  if (logical_row == clipRowCount() + 2) return RowKind::MAIN;
  return RowKind::DIRECTION;
}

int
ClipGrid::physicalFor(int logical_row) const {
  auto clip_rows = clipRowCount();
  if (logical_row <= 0) return -1;
  if (logical_row <= clip_rows) return logical_row - 1;
  if (logical_row == clip_rows + 1) return clip_rows + kSendsBottom;
  if (logical_row == clip_rows + 2) return clip_rows + kMainBottom;
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
  auto clips = song.getClips(track_id);
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

  // The editable span is the name only: the leading marker, "▸ " and clip
  // number are structural, and the trailing loop/edited icons stay too,
  // matching render()'s layout. As with a track name, the span gets a barely
  // darkened track color behind white text.
  auto clip = clips[static_cast<size_t>(clip_row)];
  bool is_edited_clip = track_clip_source_ && track_clip_source_(track_id) == clip_row;
  auto trailing = std::max(2, 2 * ((is_edited_clip ? 1 : 0) + (clip.isLooping() ? 1 : 0)));
  auto prefix_width = 3 + static_cast<int>(std::to_string(clip_row + 1).size()) + 1; // marker + "▸ " + "N "
  auto name_area_end = kColWidth - trailing;
  auto edit_width = std::max(name_area_end - prefix_width, 1);

  SongStructure structure(song);
  InlineEditor::Field field;
  field.row = physical_row;
  field.col = col_x + std::min(prefix_width, kColWidth - 1);
  field.width = edit_width;
  field.initial_text = clip.getName();
  field.text_color = getPlane().getStyles().clip_text_color;
  field.backdrop = structure.getBaselineInfo(track_id).getColor(getPlane().getStyles().tracks).blend(0.05f, Color(0, 0, 0));
  inline_editor_.open(field, [this, track_id, clip_row](std::string text) {
    auto & target_song = getController().getSong();
    auto target_clips = target_song.getClips(track_id);
    if (static_cast<size_t>(clip_row) < target_clips.size()) {
      Song::Edit edit(target_song, "rename clip");
      target_clips[static_cast<size_t>(clip_row)].setName(std::move(text));
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
    Song::Edit edit(target_song, "edit scene");
    target_song.setSceneFromText(scene, text);
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
  const int chip_width = getPlane().getStyles().track_chips ? 1 : 0;
  auto name_area_width = kColWidth - kHeaderFlagsWidth - chip_width;
  auto prefix_width = std::min(static_cast<int>(prefix.size()), name_area_width);
  auto edit_col = col_x + chip_width + prefix_width;
  auto edit_width = std::max(name_area_width - prefix_width, 1);

  InlineEditor::Field field;
  field.row = 0;
  field.col = edit_col;
  field.width = edit_width;
  field.initial_text = track->getName();
  inline_editor_.open(field, [this, track_id](std::string text) {
    auto & target_song = getController().getSong();
    if (target_song.getMasterTrack().getChildByInternalId(track_id)) {
      Song::Edit edit(target_song, "rename track");
      target_song.editTrack(track_id, [&](Track & target) { target.setName(std::move(text)); });
    }
  });
}

void
ClipGrid::activateCell(const Song & song, const std::vector<int> & track_ids) {
  auto num_tracks = static_cast<int>(track_ids.size());
  // A clip row acts exactly like a Launchpad Live View pad press
  // landing on that same cell (ClipPlayer::triggerClip(), via
  // trigger_callback_ - see setTriggerCallback()'s own comment) - an empty
  // row stops/cancels whatever the track is doing, the same as pressing an
  // unassigned pad would, so it's called unconditionally on any CLIP row
  // rather than only a populated one.
  auto kind = rowKindFor(cursor_row_);
  if (cursor_track_index_ == num_tracks) {
    // The master column: a clip row launches that scene, the Sends and Main
    // dials edit the master's sends (Main is the song's volume), the last
    // row stops every track.
    if (kind == RowKind::CLIP && scene_callback_) {
      scene_callback_(physicalFor(cursor_row_));
    } else if (kind == RowKind::SENDS || kind == RowKind::MAIN) {
      if (auto * dial = cursorDial()) dial->startEdit();
    } else if (kind == RowKind::DIRECTION && stop_all_callback_) {
      stop_all_callback_();
    }
    return;
  }
  if (cursor_track_index_ < 0 || cursor_track_index_ > num_tracks) return;
  if (kind == RowKind::SENDS || kind == RowKind::MAIN) {
    if (auto * dial = cursorDial()) dial->startEdit();
  } else if (kind == RowKind::CLIP && trigger_callback_) {
    trigger_callback_(track_ids[static_cast<size_t>(cursor_track_index_)], physicalFor(cursor_row_));
  }
  // DIRECTION: read-only for now - nothing to do.
}

ClipGrid::Pressed
ClipGrid::hitTest(int screen_y, int screen_x, int num_tracks) const {
  Pressed hit;
  auto [ pos_y, pos_x ] = getPosition();
  auto [ rows, cols ] = getDim();
  auto y = screen_y - pos_y, x = screen_x - pos_x;
  if (y < 0 || y >= rows || x < 0 || x >= cols) return hit;
  auto column = scroll_col_ + x / (kColWidth + 1);
  if (x % (kColWidth + 1) == kColWidth || column > num_tracks) return hit; // a divider, or past the last column

  auto clip_rows = clipRowCount();
  hit.column = column;
  if (y == 0) {
    hit.physical = -1;
    return hit;
  }
  auto physical = scroll_row_ + y - 1;
  if (physical < clip_rows) hit.logical_row = physical + 1;
  else if (physical >= clip_rows + kSendsTop && physical <= clip_rows + kSendsBottom) {
    // Anywhere in the group picks a dial: the left half is A's, the rest B's.
    hit.logical_row = clip_rows + 1;
    hit.send = x % (kColWidth + 1) < kDialX + kDialStride ? 0 : 1;
    physical = clip_rows + kSendsBottom;
  } else if (physical >= clip_rows + kMainTop && physical <= clip_rows + kMainBottom) {
    hit.logical_row = clip_rows + 2;
    physical = clip_rows + kMainBottom;
  } else if (physical == clip_rows + kDirectionValue) {
    hit.logical_row = clip_rows + 3;
  }
  if (hit.logical_row > 0) hit.physical = physical;
  return hit;
}

cellui::Dial & ClipGrid::dialAt(int track_id, int send) {
  auto key = (static_cast<uint64_t>(track_id) << 2) | static_cast<uint64_t>(send);
  auto & slot = dials_[key];
  if (!slot) {
    cellui::DialSpec spec;
    spec.letter = send == 0 ? "M" : send == 1 ? "A"
                                              : "B";
    spec.min = kSendFloorDb;
    spec.max = 0.0f;
    slot = std::make_unique<cellui::Dial>(getPlane(), spec);
    slot->setId((uint64_t{1} << 32) | key);
    auto * dial = slot.get();
    dial->on_change = [this, track_id, send](float db) {
      auto & controller = getController();
      if (send == 0)
        controller.setTrackSendMain(track_id, db);
      else if (send == 1)
        controller.setTrackSendA(track_id, db);
      else
        controller.setTrackSendB(track_id, db);
    };
    // A whole drag is one step of undo.
    dial->on_turn_begin = [this] { getController().getSong().document().beginGroup("turn dial"); };
    dial->on_turn_end = [this] { getController().getSong().document().endGroup(); };
    dial->on_click = [this, track_id, send] { selectDial(track_id, send); };
  }
  return *slot;
}

// Puts the cursor on a dial, the way a click on any cell does.
void ClipGrid::selectDial(int track_id, int send) {
  const Song & song = getController().getSong();
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());
  int column = num_tracks; // the master's
  for (int i = 0; i < num_tracks; i++) {
    if (track_ids[static_cast<size_t>(i)] == track_id) column = i;
  }
  cursor_track_index_ = column;
  cursor_send_ = std::max(0, send - 1);
  cursor_row_ = clipRowCount() + (send == 0 ? 2 : 1);
  view_detached_ = false;
  if (column < num_tracks) getController().getSong().setCurrentTrackId(track_id);
}

// The dial under the cursor, if it is on one.
cellui::Dial * ClipGrid::cursorDial() {
  auto kind = rowKindFor(cursor_row_);
  if (kind != RowKind::SENDS && kind != RowKind::MAIN) return nullptr;
  const Song & song = getController().getSong();
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());
  if (cursor_track_index_ < 0 || cursor_track_index_ > num_tracks) return nullptr;
  auto track_id = cursor_track_index_ < num_tracks ? track_ids[static_cast<size_t>(cursor_track_index_)] : song.getMasterTrack().getInternalId();
  return &dialAt(track_id, kind == RowKind::MAIN ? 0 : cursor_send_ + 1);
}

bool ClipGrid::isDialEditing() const {
  for (auto & [key, dial] : dials_) {
    if (dial->isEditing()) return true;
  }
  return false;
}

// The mouse for the dials: one being dragged gets all of it, wherever the
// pointer is; any other takes a press or the wheel over it.
bool ClipGrid::dialMouse(const cellui::Input & input) {
  cellui::Dial * taker = nullptr;
  for (auto & [key, dial] : dials_) {
    if (dial->isDragging()) taker = dial.get();
  }
  if (taker) {
    taker->offerMouse(input);
    mouse_down_ = taker->isDragging();
    return true;
  }
  for (auto & [key, dial] : dials_) {
    if (dial->offerMouse(input)) {
      mouse_down_ = dial->isDragging();
      return true;
    }
  }
  return false;
}

bool
ClipGrid::handleMouse(const cellui::Input & input) {
  const Song & song = getController().getSong(); // see offerInput()'s own comment on why const
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());
  auto target = hitTest(input.getY(), input.getX(), num_tracks);

  // A dial takes its own presses, drags and releases.
  if (dialMouse(input)) {
    pressed_ = Pressed();
    return true;
  }

  // A press, and each drag while held (another press), shows the cell under
  // the mouse as pressed; the release picks it.
  if (input.getKind() != InputEvent::Kind::RELEASE) {
    auto [ pos_y, pos_x ] = getPosition();
    auto [ rows, cols ] = getDim();
    auto y = input.getY() - pos_y, x = input.getX() - pos_x;
    if (y < 0 || y >= rows || x < 0 || x >= cols) return false;
    mouse_down_ = true;
    pressed_ = target;
    return true;
  }
  auto pressed = pressed_;
  mouse_down_ = false;
  pressed_ = Pressed();
  // Only what was shown pressed - a terminal that doesn't report drags
  // never moved it here.
  if (target.column < 0 || target != pressed) return true;

  cursor_track_index_ = target.column;
  if (target.logical_row == 0) return true; // the header, or a label/divider row: only the track is picked
  cursor_send_ = std::max(0, target.send); // meaningful on the Sends row only
  cursor_row_ = target.logical_row;
  view_detached_ = false;
  if (target.column < num_tracks) getController().getSong().setCurrentTrackId(track_ids[static_cast<size_t>(target.column)]); // non-const, see offerInput()'s own `song` comment

  // The dials' groups only take the cursor; the dials open their own editors.
  auto kind = rowKindFor(cursor_row_);
  if (kind != RowKind::SENDS && kind != RowKind::MAIN) activateCell(song, track_ids);
  return true;
}

// Renames the clip under the cursor, or the track when there's no clip there
// (the cursor never sits on the header row itself); on the master column's
// scene slot, the scene.
void ClipGrid::startRename() {
  const Song & song = getController().getSong();
  auto track_ids = song.getPlayableTrackIds();
  auto num_tracks = static_cast<int>(track_ids.size());
  if (cursor_track_index_ == num_tracks && rowKindFor(cursor_row_) == RowKind::CLIP) {
    startSceneRename();
    return;
  }
  auto clip_index = getCursorClipIndex();
  bool on_clip = false;
  if (clip_index >= 0 && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
    auto clips = song.getClips(track_ids[static_cast<size_t>(cursor_track_index_)]);
    on_clip = clip_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(clip_index)].isEmpty();
  }
  if (on_clip)
    startClipRename(song, track_ids);
  else
    startTrackRename(song, track_ids);
}

bool
ClipGrid::offerInput(const cellui::Input & input) {
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
  for (auto & [key, dial] : dials_) {
    if (!dial->isEditing()) continue;
    // A click anywhere else closes the editor (and then does what it would);
    // the other mouse input is ignored while one is open, every key is its own.
    if (input.getId() == Key::Button1) {
      if (input.getKind() != InputEvent::Kind::RELEASE && !dial->contains(input.getY(), input.getX()))
        dial->cancelEdit();
      else
        return true;
    } else if (input.getId() == Key::Button4 || input.getId() == Key::Button5) {
      return true;
    } else {
      return dial->offerInput(input);
    }
  }

  if (input.getId() == Key::Button1) return handleMouse(input);

  // The mouse wheel scrolls the view (Shift: tracks), not the cursor, until
  // the cursor next moves.
  if (input.getId() == Key::Button4 || input.getId() == Key::Button5) {
    if (input.getKind() == InputEvent::Kind::RELEASE) return true;
    // Over a dial the wheel turns it.
    if (dialMouse(input)) return true;
    int direction = input.getId() == Key::Button4 ? -1 : 1;
    if (input.hasShift()) scroll_col_ += direction;
    else scroll_row_ += direction;
    view_detached_ = true;
    return true;
  }

  if (dispatchCommand(input)) return true;
  if (input.getKind() == InputEvent::Kind::RELEASE) return false;

  // Navigation, plus the manual (non-command) part of this widget's own
  // input handling: Enter (clip focus toggle, on a clip row),
  // 'l' (toggle its own loop flag) - Mute/
  // Solo are dispatchCommand()'s job now, handled above via this
  // constructor's own toggle-mute/toggle-solo bindings.
  if (input.getId() == Key::Up) cursor_row_--;
  else if (input.getId() == Key::Down) cursor_row_++;
  else if (input.getId() == Key::Left || input.getId() == Key::Right) {
    int step = input.getId() == Key::Left ? -1 : 1;
    bool on_sends = rowKindFor(cursor_row_) == RowKind::SENDS;
    // On the Sends row the two dials are stops of their own: Left and Right
    // walk through them, then on into the next column.
    if (on_sends && cursor_send_ + step >= 0 && cursor_send_ + step <= 1)
      cursor_send_ += step;
    else if (cursor_track_index_ + step >= 0 && cursor_track_index_ + step <= num_tracks) {
      cursor_track_index_ += step;
      cursor_send_ = step > 0 ? 0 : 1;
    }
  }
  else if (input.getId() == Key::PageUp) cursor_row_ -= getDim().first;
  else if (input.getId() == Key::PageDown) cursor_row_ += getDim().first;
  else if (input.getId() == 'l' && !input.hasCtrl() && !input.hasAlt()) {
    // Loop toggle - only meaningful on a clip row that actually has a
    // clip; a no-op (but still consumed) anywhere else.
    if (rowKindFor(cursor_row_) == RowKind::CLIP && cursor_track_index_ >= 0 && cursor_track_index_ < num_tracks) {
      auto track_id = track_ids[static_cast<size_t>(cursor_track_index_)];
      auto clips = song.getClips(track_id);
      auto clip_row = physicalFor(cursor_row_); // a CLIP row's own physical offset doubles as its clip-list index
      if (clip_row >= 0 && static_cast<size_t>(clip_row) < clips.size() && !clips[static_cast<size_t>(clip_row)].isEmpty()) {
        auto & mutable_song = getController().getSong(); // non-const - this branch genuinely writes
        auto clip = mutable_song.getClips(track_id)[static_cast<size_t>(clip_row)];
        Song::Edit edit(mutable_song, "toggle clip loop");
        clip.setLooping(!clip.isLooping());
      }
    }
    return true;
  } else if (input.getId() == Key::Enter) {
    activateCell(song, track_ids);
    return true;
  } else
    return false;

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
  // The cursor shows only while the window has the focus.
  focused = focused && styles.window_focused;
  // Losing the focus closes a dial's editor.
  if (!focused) {
    for (auto & [key, dial] : dials_) dial->cancelEdit();
  }
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

  // How focused the grid is, eased: the cursor fades in and out with it.
  focus_ = fades_.value(0, focused ? 1.0f : 0.0f, !styles.reduced_motion);
  // One more frame after it ends draws the final state.
  bool fading = fades_.active() || focus_fade_drawn_;
  focus_fade_drawn_ = fades_.active();
  fades_.sweep();
  auto new_version = song.getMajorVersion();
  bool editor_redraw = inline_editor_.consumeRedrawRequest();
  // What the dials show: pressed or not, and what is being typed.
  uint64_t dials_state = 0;
  for (auto & [key, dial] : dials_) dials_state += dial->version() + (dial->isPressed() ? 1u << 20 : 0u);
  bool dials_changed = dials_state != current_dials_state_;
  current_dials_state_ = dials_state;
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

  // The meters, in 1/256ths of the meter (negative while clipping) - only
  // a change a pixel meter could show redraws.
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
    auto steps = static_cast<int>(meter.fraction * 256.0f + 0.5f);
    meter_steps.push_back(track_info.isClipping() ? -1 - steps : steps);
    meter_steps.push_back(static_cast<int>(meter.peak_fraction * 256.0f + 0.5f));
  }
  bool meters_changed = meter_steps != current_meter_steps_;
  current_meter_steps_ = std::move(meter_steps);

  if (!refresh && !editor_redraw && !dials_changed && !clip_states_changed && !meters_changed && !fading && new_version == current_song_version_ &&
      cursor_track_index_ == current_cursor_track_index_ && cursor_row_ == current_cursor_row_ && cursor_send_ == current_cursor_send_ &&
      scroll_col_ == current_scroll_col_ && scroll_row_ == current_scroll_row_ &&
      focused == current_focused_ && focused_clip_id == current_focused_clip_id_ && pressed_ == current_pressed_ &&
      pressed_.physical == current_pressed_.physical) {
    return false;
  }
  current_focused_clip_id_ = focused_clip_id;
  current_pressed_ = pressed_;
  current_song_version_ = new_version;
  current_cursor_track_index_ = cursor_track_index_;
  current_cursor_row_ = cursor_row_;
  current_cursor_send_ = cursor_send_;
  current_scroll_col_ = scroll_col_;
  current_scroll_row_ = scroll_row_;
  current_focused_ = focused;

  // The dials are placed again below; one that is not (scrolled out of view,
  // or past the last column) takes no input.
  for (auto & [key, dial] : dials_) dial->setClip(0, 0);

  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  erase();
  // erase() alone leaves this plane's own base cell showing through
  // wherever nothing below explicitly writes a cell (its own semi-
  // transparent debug base, Plane::createChild()) - this widget shares
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
      renderMasterColumn(styles, vc * (kColWidth + 1), rows, focus_ > 0.0f, num_tracks, sceneState);
      renderMeter(styles, vc * (kColWidth + 1), rows, master_id, master_info.isClipping());
      break;
    }
    if (track_index > num_tracks) break;
    auto track_id = track_ids[static_cast<size_t>(track_index)];
    auto x = vc * (kColWidth + 1);
    auto * leaf = asLeafTrack(song, track_id);
    auto clips = song.getClips(track_id);

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
    // With chips the track's colour is a half-cell slice at the left of the
    // header (StyleProvider::trackChipColor()).
    const int chip_width = styles.track_chips ? 1 : 0;
    auto name_width = kColWidth - kHeaderFlagsWidth - chip_width;
    // -1 is the header row's own physical value (physicalFor()'s own
    // comment) - never a real physical_row inside the loop below, so this
    // is the header's one and only cursor check.
    bool header_is_cursor = focus_ > 0.0f && track_index == cursor_track_index_ && cursor_physical == -1;
    bool header_pressed = isPressed(track_index, -1);
    setFgColor(header_pressed ? styles.highlight_fg_color
               : header_is_cursor ? styles.window_accent_fg_color.blend(focus_, styles.highlight_fg_color)
                                  : styles.window_accent_fg_color);
    setBgColor(header_pressed ? styles.row_pressed_bg_color
               : header_is_cursor ? styles.heading_bg_color.blend(focus_, styles.highlight_bg_color)
                                  : styles.heading_bg_color);
    putstr(0, x, string(static_cast<size_t>(kColWidth), ' ')); // opaque header background first, across the whole column
    auto prefix = "T" + std::to_string(structure.getBaselineInfo(track_id).color_ordinal_);
    auto friendly_name = (leaf && !leaf->getName().empty()) ? " " + leaf->getName() : string();
    auto header = Utf8::padToWidth(Utf8::truncateToWidth(prefix + friendly_name, name_width), name_width);
    auto upright_len = std::min(header.size(), prefix.size());
    putstr(0, x + chip_width, header.substr(0, upright_len));
    if (upright_len < header.size()) {
      setItalic(true);
      putstr(0, x + chip_width + static_cast<int>(upright_len), header.substr(upright_len));
      setItalic(false);
    }
    if (chip_width > 0) {
      setFgColor(styles.trackChipColor(structure.getBaselineInfo(track_id).color_ordinal_));
      putstr(0, x, "▌");
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
      bool is_cursor_cell = focus_ > 0.0f && track_index == cursor_track_index_ && physical_row == cursor_physical;
      // The cursor track's clip being edited in the pattern editor below
      // is marked with a pencil.
      bool is_edited_clip = track_clip_source_ && track_index == cursor_track_index_ && physical_row < clip_rows && physical_row == track_clip_source_(track_id);

      // The rows of the Sends and Main groups are the dials': a plain
      // background here (and the Sends caption), the dials drawn over it
      // after the loop.
      if ((physical_row >= clip_rows + kSendsTop && physical_row <= clip_rows + kSendsBottom) ||
          (physical_row >= clip_rows + kMainTop && physical_row <= clip_rows + kMainBottom)) {
        putDialGroupRow(styles, y, x, physical_row - clip_rows);
        continue;
      }

      // A clip row's own trailing repeat glyph (U+21BB) renders wider in
      // this terminal than this codebase's own width tools (Utf8::
      // displayWidth(), and so the frame encoder's own column-advance) can
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
        Color row_fg = styles.clip_text_color, row_bg = styles.grid_bg_color;
        string text;
        // Content-aware, not just in-bounds - holes are allowed (Song::
        // ensureClipAt()), so an in-bounds but still-empty filler renders
        // as the plain "nothing here" stop icon below, same as a row
        // genuinely past the list's own end.
        bool has_real_clip = clip_row < clips.size() && !clips[clip_row].isEmpty();
        if (has_real_clip) {
          auto clip = clips[clip_row];
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
          row_bg = structure.getBaselineInfo(track_id).getColor(styles.tracks);
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
          // Slightly brighter than the grid, so the empty slots stand out as
          // slots, apart from the sends below.
          row_bg = styles.grid_bg_color.blend(0.07f, Color(255, 255, 255));
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
                                                  : dark.blend(statePulse(state, styles.reduced_motion), bright);
        }
        if (is_cursor_cell && !has_real_clip) {
          row_fg = row_fg.blend(focus_, styles.highlight_fg_color);
          row_bg = row_bg.blend(focus_, styles.highlight_bg_color);
        } else if (is_cursor_cell) {
          // A clip's background is its track's identity color - the cursor
          // brightens it rather than replacing it, the same as
          // ArrangementGrid's cursor on its colored instance cells.
          row_fg = row_fg.blend(focus_, styles.highlight_fg_color);
          row_bg = row_bg.blend(0.5f * focus_, styles.cursor_tint_color);
        }
        if (isPressed(track_index, physical_row)) {
          row_fg = styles.highlight_fg_color;
          row_bg = styles.row_pressed_bg_color;
        }
        // A clip stands on the grid like a card. The cursor stands as high,
        // so no clip's shadow falls on it, but casts none of its own.
        if (is_cursor_cell)
          setLift(styles.theme.elevation.card, false, false);
        else
          setLift(styles.theme.elevation.card);
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
        setLift(0);
        continue;
      }

      string text;
      Color fg = styles.window_fg_color, bg = styles.grid_bg_color;
      bool is_divider = physical_row == clip_rows + kSendsDivider || physical_row == clip_rows + kMainDivider ||
                        physical_row == clip_rows + kDirectionDivider;

      if (is_divider) {
        text = dividerLine(styles, physical_row - clip_rows);
        fg = styles.window_border_color;
      } else if (physical_row == clip_rows + kDirectionLabel) { // Direction label (decorative)
        text = fmt::format("{:>4}{:>5}{:>6}", "Az", "El", "Dist");
        fg = styles.window_accent_fg_color;
      } else if (physical_row == clip_rows + kDirectionValue && leaf) { // Direction value, azimuth/elevation/distance on one line
        text = fmt::format("{:>4.0f}{:>5.0f}{:>6.1f}", leaf->getAzimuth(), leaf->getElevation(), leaf->getDistance());
      }
      text = Utf8::padToWidth(Utf8::truncateToWidth(text, kColWidth), kColWidth);

      // Only the Direction value row ever reaches here with is_cursor_cell
      // true - the dial groups and every clip row are handled directly above,
      // each with their own cursor treatment.
      if (is_cursor_cell) {
        fg = fg.blend(focus_, styles.highlight_fg_color);
        bg = bg.blend(focus_, styles.highlight_bg_color);
      }
      if (isPressed(track_index, physical_row)) {
        fg = styles.highlight_fg_color;
        bg = styles.row_pressed_bg_color;
      }
      setFgColor(fg);
      setBgColor(bg);
      putstr(y, x, text);
    }

    auto & track_info = playback_info.getTrackInfo(track_id);
    if (leaf) renderDials(styles, x, rows, track_index, track_id, leaf->getSends());
    renderMeter(styles, x, rows, track_id, track_info.isClipping());

    setFgColor(styles.window_border_color);
    setBgColor(styles.heading_bg_color); // the header row's own backdrop, not the plain window background below it
    putDivider(styles, 0, x + kColWidth, true);
    // Dividers belong to no track, so no track's clip mark reaches them.
    for (auto y = 1; y < rows; y++) putDivider(styles, y, x + kColWidth, false);
  }

  inline_editor_.paintBackdrop();
  return true;
}

void
ClipGrid::renderMeter(const StyleProvider & styles, int x, int rows, int track_id, bool clipping) {
  auto & meter = meters_[track_id];
  auto top = clipRowCount() + kMainTop - scroll_row_ + 1; // the meter's top cell; +1 for the header row
  cellui::VuReading reading{meter.fraction, meter.peak_fraction, clipping};
  cellui::VuMeterLook look{styles.grid_bg_color, std::nullopt};
  // Below the header row, inside the plane.
  if (scopes_) scopes_->drawVuMeter(getPlane(), top, x + kColWidth - 1, kMeterRows, reading, look, 1, rows, static_cast<uint64_t>(track_id) + 2);
  else cellui::drawCellVuMeter(getPlane(), top, x + kColWidth - 1, kMeterRows, reading, look, 1, rows);
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
    Color fg = styles.window_fg_color, bg = styles.grid_bg_color;
    if (is_cursor_cell) {
      fg = fg.blend(focus_, styles.highlight_fg_color);
      bg = bg.blend(focus_, styles.highlight_bg_color);
    }
    if (isPressed(num_tracks, physical_row)) {
      fg = styles.highlight_fg_color;
      bg = styles.row_pressed_bg_color;
    }

    if (physical_row < clip_rows) {
      // A scene slot: launches clip row k on every track, showing the
      // scene's name after the launch glyph; dim when no track has a clip
      // there (launching it just stops everything).
      bool has_any_clip = false;
      for (auto track_id : track_ids) {
        auto clips = song.getClips(track_id);
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
      // The cursor above the clips' shadows, as on a track's slots.
      if (is_cursor_cell) setLift(styles.theme.elevation.card, false, false);
      putstr(y, x, Utf8::padToWidth(" ▸ " + Utf8::padToWidth(Utf8::truncateToWidth(song.getSceneName(physical_row), name_width), name_width) + tempo_text, kColWidth));
      auto state = scene_state(physical_row);
      if (state == ClipHighlight::PLAYING || state == ClipHighlight::QUEUED || state == ClipHighlight::PAUSED) {
        setFgColor(state == ClipHighlight::PAUSED ? styles.clip_paused_color : styles.clip_playing_color);
        putstr(y, x + 1, state == ClipHighlight::QUEUED ? "▹" : "▸");
      }
      setLift(0);
      continue;
    }

    auto offset = physical_row - clip_rows;
    if ((offset >= kSendsTop && offset <= kSendsBottom) || (offset >= kMainTop && offset <= kMainBottom)) {
      // The master's own Send Main/A/B: the dry mix and the send bus's two
      // returns - the same dials, and parameters, a track has.
      putDialGroupRow(styles, y, x, offset);
      continue;
    }
    std::string text;
    if (offset == kSendsDivider || offset == kMainDivider || offset == kDirectionDivider) {
      text = dividerLine(styles, offset);
      fg = styles.window_border_color;
    } else if (offset == kDirectionValue) {
      // The master has no position; its second addressable row stops
      // every track instead.
      text = " ⏹ Stop all";
    }
    setFgColor(fg);
    setBgColor(bg);
    putstr(y, x, Utf8::padToWidth(text, kColWidth));
  }

  renderDials(styles, x, rows, num_tracks, master.getInternalId(), master.getSends());

  // The trailing divider every column has, the header's in its backdrop.
  putDivider(styles, 0, x + kColWidth, true);
  for (auto y = 1; y < rows && scroll_row_ + y - 1 < physicalRowCount(); y++) putDivider(styles, y, x + kColWidth, false);
}

// The column after a track: a line in the header's backdrop and the window's,
// or a gutter of the window's own colour when the theme has no grid lines.
void
ClipGrid::putDivider(const StyleProvider & styles, int y, int x, bool header) {
  setFgColor(styles.window_border_color);
  setBgColor(header && styles.grid_lines ? styles.heading_bg_color : styles.window_bg_color);
  putstr(y, x, styles.grid_lines ? "│" : " ");
}

// A divider row's text: the lines of the grid when the theme has them; the one
// under the Sends is always a rule, setting them apart from the track's other
// parameters.
std::string ClipGrid::dividerLine(const StyleProvider & styles, int offset) const {
  if (offset == kMainDivider) {
    std::string rule;
    for (int i = 0; i < kColWidth; i++) rule += "─";
    return rule;
  }
  return std::string(static_cast<size_t>(kColWidth), styles.grid_lines ? '-' : ' ');
}

void ClipGrid::putDialGroupRow(const StyleProvider & styles, int y, int x, int offset) {
  setFgColor(styles.window_fg_color);
  setBgColor(styles.grid_bg_color);
  putstr(y, x, std::string(static_cast<size_t>(kColWidth), ' '));
  // The caption stands in the free corner above B.
  if (offset == kSendsTop) {
    setFgColor(styles.window_accent_fg_color);
    putstr(y, x + kDialX + kDialStride + 1, "Sends");
  }
}

void ClipGrid::renderDials(const StyleProvider & styles, int x, int rows, int column, int track_id, const SendLevels & sends) {
  auto clip_rows = clipRowCount();
  bool cursor_column = focus_ > 0.0f && column == cursor_track_index_;
  auto kind = rowKindFor(cursor_row_);
  struct Slot {
    int send, offset, dx; // 0 Main, 1 A, 2 B; the slot's first row below the clip rows, and its first cell in the column
    float level;
    bool on; // under the cursor
  };
  const Slot slots[] = {
      {1, kSendsTop, kDialX, sends.a, cursor_column && kind == RowKind::SENDS && cursor_send_ == 0},
      {2, kSendsBTop, kDialX + kDialStride, sends.b, cursor_column && kind == RowKind::SENDS && cursor_send_ == 1},
      {0, kMainTop, kMainX, sends.main, cursor_column && kind == RowKind::MAIN},
  };
  for (auto & slot : slots) {
    auto & dial = dialAt(track_id, slot.send);
    dial.setPosition(clip_rows + slot.offset - scroll_row_ + 1, x + slot.dx);
    dial.setClip(1, rows);
    dial.setLook(styles.window_accent_fg_color, styles.grid_bg_color);
    dial.setValue(sendLinearToDb(slot.level));
    dial.setSelected(slot.on ? focus_ : 0.0f);
    dial.draw();
  }
}
