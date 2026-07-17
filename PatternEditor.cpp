#include "PatternEditor.h"

#include "InputEvent.h"
#include "SongState.h"
#include "Controller.h"
#include "StyleProvider.h"
#include "Tuner.h"
#include "Tuning.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"
#include "MidiEvent.h"
#include "LaunchpadPadEvent.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadManager.h"
#include "PlaybackControlEvent.h"
#include "LogEvent.h"
#include "KeyChord.h"

#include <string>
#include <algorithm>
#include <fmt/core.h>

#include <iostream>

using namespace std;
using namespace fmt;

static void get_root_track_ids(const Track & track, vector<int> & track_ids) {
  if (track.getType() == TrackType::INSTRUMENT_CONTROL ||
      track.getType() == TrackType::PERCUSSION_CONTROL ||
      track.getType() == TrackType::SAMPLE) {
    track_ids.push_back(track.getInternalId());
  } else {
    for (auto & child : track.getChildren()) {
      get_root_track_ids(*child, track_ids);
    }
  }
}

static void get_root_track_ids(const Song & song, vector<int> & track_ids) {
  for (auto & child : song.getTracks()) {
    get_root_track_ids(*child, track_ids);
  }
}

PatternEditor::PatternEditor(UIPlane & parent) : UIElement(parent) {
  // getPlane().setScrolling(true);

  // Emacs-style commands, dispatched centrally via UIElement::dispatchCommand
  // (see offerInput() below). Each lambda re-fetches song/info/track_ids
  // itself, exactly like the code that used to run inline here did on every
  // call - none of this is cached across calls, so there's no staleness risk
  // from moving it into a constructor-time closure.

  commands_.define("set-mark", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    vector<int> track_ids;
    get_root_track_ids(song, track_ids);
    selection_start_pattern_ = info.getPatternIndex();
    selection_start_row_ = info.getRowIndex();
    selection_start_track_ = current_cursor.track;
    // A fresh mark starts scoped to just the note column it's set on;
    // moving sideways afterward widens/narrows to the touched range (see
    // kill-region etc.) - to select the whole track, widen across all of
    // its note columns.
    auto track_info = getTrackInfoFor(song, track_ids[current_cursor.track]);
    selection_start_note_ = clamp(track_info.getNoteNumber(current_cursor.col), 0, max(track_info.num_subtracks_ - 1, 0));
    selection_active_ = true;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Mark set"));
  });

  // Both commands below always have a region to act on, even with no mark
  // active: it degenerates to the single note the cursor is currently on
  // (see getEffectiveSelectionBounds) - there's no "No selection" case.
  commands_.define("kill-region", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    vector<int> track_ids;
    get_root_track_ids(song, track_ids);
    auto & event_queue = getController().getPlaybackEventQueue();
    auto & pattern = song.getPattern(info.getPatternIndex());

    auto b = getEffectiveSelectionBounds(song, track_ids);
    clipboard_column_scoped_ = b.column_scoped;
    clipboard_includes_command_ = b.includes_command;
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      clipboard_ = copyPatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, b.includes_command);
      clearPatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, b.includes_command);
    } else {
      clipboard_ = copyPatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
      clearPatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
    }
    song.incVersion();
    selection_active_ = false;
    // move point to the start of the killed region, matching Emacs
    // kill-region, so an immediate yank restores it exactly in place
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, b.row_lo - info.getRowIndex()));
    new_cursor.track = b.track_lo;
    if (!b.column_scoped) {
      // Only reset to the first column for a whole-track kill; a
      // column-scoped kill (e.g. just note column 2) should leave the
      // cursor on the column it was already on, not jump back to 0.
      new_cursor.col = new_cursor.subcol = 0;
    } else {
      // Killing the track's last remaining voice can shrink its note-column
      // count (num_subtracks_ is derived from the widest row left in the
      // pattern). A raw index-bounds check isn't enough here: the old
      // index can still be "in range" of the narrower layout while meaning
      // something entirely different now (getNoteNumber() mechanically
      // extrapolates past the end the same way it does for the effect
      // column, so a stale index can silently resolve to the effect
      // column instead of clamping) - check via note number instead, and
      // snap to the corresponding sub-column of the last remaining voice.
      auto new_track_info = getTrackInfoFor(song, track_ids[b.track_lo]);
      auto new_max_note = max(new_track_info.num_subtracks_ - 1, 0);
      if (new_track_info.getNoteNumber(new_cursor.col) > new_max_note) {
        auto n = (new_track_info.has_note_column_ ? 1 : 0) + new_track_info.num_velocity_columns_ +
          (new_track_info.has_delay_column_ ? 1 : 0);
        new_cursor.col = new_max_note * n;
        new_cursor.subcol = 0;
      }
    }
    getController().getUIEventQueue().push(make_unique<LogEvent>("Region killed"));
  });

  commands_.define("kill-ring-save", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    vector<int> track_ids;
    get_root_track_ids(song, track_ids);
    auto & pattern = song.getPattern(info.getPatternIndex());

    auto b = getEffectiveSelectionBounds(song, track_ids);
    clipboard_column_scoped_ = b.column_scoped;
    clipboard_includes_command_ = b.includes_command;
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      clipboard_ = copyPatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, b.includes_command);
    } else {
      clipboard_ = copyPatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi);
    }
    selection_active_ = false;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Region copied"));
  });

  commands_.define("yank", [this]() {
    if (!clipboard_.empty()) {
      auto & song = getController().getSong();
      auto & info = getController().getPlaybackInfo();
      vector<int> track_ids;
      get_root_track_ids(song, track_ids);
      auto & pattern = song.getPattern(info.getPatternIndex());
      if (clipboard_column_scoped_) {
        auto track_id = track_ids[current_cursor.track];
        auto track_info = getTrackInfoFor(song, track_id);
        auto target_note = clamp(track_info.getNoteNumber(current_cursor.col), 0, max(track_info.num_subtracks_ - 1, 0));
        pastePatternBlockNotes(pattern, clipboard_, info.getRowIndex(), track_id, target_note, clipboard_includes_command_);
      } else {
        pastePatternBlock(pattern, clipboard_, info.getRowIndex(), track_ids, current_cursor.track);
      }
      song.incVersion();
      getController().getUIEventQueue().push(make_unique<LogEvent>("Yanked"));
    } else {
      getController().getUIEventQueue().push(make_unique<LogEvent>("Clipboard empty"));
    }
  });

  commands_.define("keyboard-quit", [this]() {
    if (selection_active_) {
      selection_active_ = false;
      getController().getUIEventQueue().push(make_unique<LogEvent>("Mark deactivated"));
    }
  });

  // Unlike kill-region/kill-ring-save, transpose isn't destructive, so it
  // never clears the mark - repeated presses keep transposing the same
  // region. With no mark active, the region degenerates to the single
  // note under the cursor (see getEffectiveSelectionBounds) - to transpose
  // the whole pattern, select all of it first.
  commands_.define("transpose-region-up", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & pattern = song.getPattern(info.getPatternIndex());
    vector<int> track_ids;
    get_root_track_ids(song, track_ids);

    auto b = getEffectiveSelectionBounds(song, track_ids);
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      transposePatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, true);
    } else {
      transposePatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi, true);
    }
    song.incVersion();
  });

  commands_.define("transpose-region-down", [this]() {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & pattern = song.getPattern(info.getPatternIndex());
    vector<int> track_ids;
    get_root_track_ids(song, track_ids);

    auto b = getEffectiveSelectionBounds(song, track_ids);
    if (b.column_scoped) {
      auto track_id = track_ids[b.track_lo];
      transposePatternBlockNotes(pattern, b.row_lo, b.row_hi, track_id, b.note_lo, b.note_hi, false);
    } else {
      transposePatternBlock(pattern, b.row_lo, b.row_hi, track_ids, b.track_lo, b.track_hi, false);
    }
    song.incVersion();
  });

  keymap_.bind(KeyChord::pack(' ', true, false, false, false), "set-mark");  // Ctrl-Space
  keymap_.bind(KeyChord::pack('b', true, false, false, false), "set-mark");  // Ctrl-B (see todo.txt; works on any terminal)
  keymap_.bind(KeyChord::pack('w', true, false, false, false), "kill-region");
  keymap_.bind(KeyChord::pack('w', false, true, false, false), "kill-ring-save");  // Alt-W
  keymap_.bind(KeyChord::pack('y', true, false, false, false), "yank");
  keymap_.bind(KeyChord::pack('g', true, false, false, false), "keyboard-quit");
  keymap_.bind(KeyChord::pack(NCKEY_UP, true, false, true, false), "transpose-region-up");    // Ctrl+Shift+Up
  keymap_.bind(KeyChord::pack(NCKEY_DOWN, true, false, true, false), "transpose-region-down"); // Ctrl+Shift+Down

  assertCommandBindingsValid();
}

static void fill_track_info(const Track & track, std::unordered_map<int, VisibleTrackInfo> & track_info) {
  if (track.getType() == TrackType::INSTRUMENT_CONTROL ||
      track.getType() == TrackType::PERCUSSION_CONTROL) {
    auto & info = track_info[track.getInternalId()];
    auto & instrument_track = dynamic_cast<const InstrumentTrack&>(track);
    info.has_note_column_ = instrument_track.showNoteColumn();
    info.num_velocity_columns_ = instrument_track.showVelocityColumn() ? 1 : 0;
    info.has_delay_column_ = instrument_track.showDelayColumn();
    info.has_effect_column_ = instrument_track.showEffectsColumn();
  } else {
    for (auto & child : track.getChildren()) {
      fill_track_info(*child, track_info);
    }
  }
}

static void get_track_parents(Track & track, Track * parent, std::unordered_map<int, Track *> & parents) {
  parents[track.getInternalId()] = parent;
  for (auto & child : track.getChildren()) {
    get_track_parents(*child, &track, parents);
  }
}

std::unordered_map<int, VisibleTrackInfo>
PatternEditor::getTrackInformation(const Song & song) const {
  auto [rows, cols] = getDim();
  auto heading_height = song.getTrackDepth() + 1;
  auto & info = getController().getPlaybackInfo();

  std::unordered_map<int, VisibleTrackInfo> track_info;
  for (auto row = 0; row < rows - heading_height; ) {
    auto [ pattern_idx, pattern_row ] = song.normalizePosition(info.getPatternIndex(), row + current_scroll_row);
    if (pattern_idx >= song.getPatterns().size()) break;
    
    auto & pattern = song.getPattern(pattern_idx);
    pattern.getTrackInformation(track_info);
    row += pattern.getNumRows() - pattern_row;
  }
  for (auto & track : song.getTracks()) {
    fill_track_info(*track, track_info);
  }

  return track_info;
}

VisibleTrackInfo
PatternEditor::getTrackInfoFor(const Song & song, int track_id) const {
  auto all_track_info = getTrackInformation(song);
  auto it = all_track_info.find(track_id);
  return it != all_track_info.end() ? it->second : VisibleTrackInfo();
}

PatternEditor::SelectionBounds
PatternEditor::getEffectiveSelectionBounds(const Song & song, const vector<int> & track_ids) const {
  auto & info = getController().getPlaybackInfo();
  bool has_mark = selection_active_ && selection_start_pattern_ == info.getPatternIndex();

  SelectionBounds b;
  auto start_row = has_mark ? selection_start_row_ : info.getRowIndex();
  auto start_track = has_mark ? selection_start_track_ : current_cursor.track;
  b.row_lo = min(start_row, info.getRowIndex());
  b.row_hi = max(start_row, info.getRowIndex());
  b.track_lo = min(start_track, current_cursor.track);
  b.track_hi = max(start_track, current_cursor.track);
  b.column_scoped = b.track_lo == b.track_hi;

  if (b.column_scoped) {
    auto track_info = getTrackInfoFor(song, track_ids[b.track_lo]);
    auto max_note = max(track_info.num_subtracks_ - 1, 0);
    if (track_info.isEffectColumn(current_cursor.col)) {
      // The effect command applies to every note column in the row, not
      // just one - there's no such thing as a partial effect-column
      // region, so widen to all of them regardless of any mark, and mark
      // the row's Command as part of the region too.
      b.note_lo = 0;
      b.note_hi = max_note;
      b.includes_command = true;
    } else {
      auto point_note = clamp(track_info.getNoteNumber(current_cursor.col), 0, max_note);
      auto start_note = has_mark ? clamp(selection_start_note_, 0, max_note) : point_note;
      b.note_lo = min(start_note, point_note);
      b.note_hi = max(start_note, point_note);
    }
  } else {
    b.note_lo = b.note_hi = 0;
  }

  return b;
}

bool
PatternEditor::render(const StyleProvider & styles, bool refresh) {
  bool render_all = refresh;
  auto & info = getController().getPlaybackInfo();
  auto score_pattern = info.getPatternIndex();
  auto score_playing_row = info.getRowIndex();
  auto & song = getController().getSong();

  if (selection_active_ && selection_start_pattern_ != score_pattern) {
    selection_active_ = false;
    getController().getUIEventQueue().push(make_unique<LogEvent>("Selection cleared: crossed pattern boundary"));
  }
  
  auto track_info = getTrackInformation(song);

  auto [rows, cols] = getDim();
  auto heading_height = song.getTrackDepth() + 1;
  
  vector<int> track_ids;
  get_root_track_ids(song, track_ids);

  if (launchpad_manager_) {
    launchpad_manager_->refresh(song, track_ids, info,
      track_ids.empty() ? -1 : new_cursor.track);
  }

  auto score_total_columns = 0;
  for (auto wd : track_info) score_total_columns += wd.second.getColumnCount();

  auto new_scroll_row = current_scroll_row;
  if (score_playing_row < new_scroll_row) {
    new_scroll_row = score_playing_row;
  } else if (score_playing_row >= new_scroll_row + rows - heading_height) {
    new_scroll_row = score_playing_row - (rows - heading_height) + 1;
  }

  auto new_scroll_track = current_scroll_track;
  if (new_cursor.track < new_scroll_track) {
    new_scroll_track = new_cursor.track;
  } else {
    while ( 1 ) {
      auto pos = 6;
      for (auto i = new_scroll_track; i < track_ids.size() && i <= new_cursor.track; i++) {
	auto id = track_ids[i];
	auto it = track_info.find(id);
	pos += (it != track_info.end() ? it->second.getTrackWidth() : 0);
      }
      if (pos >= cols) {
	new_scroll_track++;
      } else {
	break;
      }
    }
  }

  bool selection_changed = selection_active_ != current_selection_active_ ||
      (selection_active_ && (selection_start_pattern_ != current_selection_start_pattern_ ||
			     selection_start_row_ != current_selection_start_row_ ||
			     selection_start_track_ != current_selection_start_track_ ||
			     selection_start_note_ != current_selection_start_note_ ||
			     score_playing_row != current_score_playing_row ||
			     new_cursor.track != current_cursor.track ||
			     new_cursor.col != current_cursor.col));

  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_scroll_row != current_scroll_row ||
      new_scroll_track != current_scroll_track ||
      selection_changed
      ) {
    render_all = true;
  }
  
  bool cursor_changed = new_cursor.track != current_cursor.track || new_cursor.col != current_cursor.col || new_cursor.subcol != current_cursor.subcol;
  
  current_cursor.track = new_cursor.track;
  current_cursor.col = new_cursor.col;
  current_cursor.subcol = new_cursor.subcol;

  // Always something to highlight - degenerates to just the note under the
  // cursor when no mark is set (see getEffectiveSelectionBounds). Computed
  // after current_cursor is updated above, so it reflects where the cursor
  // just moved *to* this frame, not where it was before.
  auto sel_bounds = getEffectiveSelectionBounds(song, track_ids);

  bool need_redraw = false;
  if (render_all) {
    current_scroll_row = new_scroll_row;
    current_scroll_track = new_scroll_track;
    
    erase();
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    fill();
    
    renderHeading(styles, track_ids, track_info);
    for (auto row = 0; row < rows - heading_height; row++) {
      renderRow(styles, heading_height, track_ids, track_info, row, (row + current_scroll_row) == score_playing_row, sel_bounds);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderHeading(styles, track_ids, track_info);
    renderRow(styles, heading_height, track_ids, track_info, current_score_playing_row - current_scroll_row, false, sel_bounds);
    renderRow(styles, heading_height, track_ids, track_info, score_playing_row - current_scroll_row, true, sel_bounds);
    need_redraw = true;
  } else if (cursor_changed || row_edited) {
    renderRow(styles, heading_height, track_ids, track_info, score_playing_row - current_scroll_row, true, sel_bounds);
    auto extra_redraw_row = launchpad_manager_ ? launchpad_manager_->extraRedrawRow() : -1;
    if (extra_redraw_row >= 0 && extra_redraw_row != score_playing_row) {
      auto extra_screen_row = extra_redraw_row - current_scroll_row;
      if (extra_screen_row >= 0 && extra_screen_row < rows - heading_height) {
	renderRow(styles, heading_height, track_ids, track_info, extra_screen_row, false, sel_bounds);
      }
    }
    need_redraw = true;
  }

  int new_tempo = song.getTempo();
  
  if (render_all || edit_step_size != new_edit_step_size || new_tempo != current_tempo || cursor_changed) {    
    edit_step_size = new_edit_step_size;
    current_tempo = new_tempo;
    
    need_redraw = true;
  }
  
  current_score_pattern = score_pattern;
  current_score_playing_row = score_playing_row;
  current_score_total_columns = score_total_columns;
  current_song_version = song.getVersion();
  row_edited = false;
  if (launchpad_manager_) launchpad_manager_->clearExtraRedrawRow();

  current_selection_active_ = selection_active_;
  current_selection_start_pattern_ = selection_start_pattern_;
  current_selection_start_row_ = selection_start_row_;
  current_selection_start_track_ = selection_start_track_;
  current_selection_start_note_ = selection_start_note_;
  
  return need_redraw;
}

void
PatternEditor::handleMidiEvent(MidiEvent & ev) {
  auto & event_queue = getController().getPlaybackEventQueue();

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();

  bool was_playing = !active_midi_notes.empty();

  vector<int> track_ids;
  get_root_track_ids(song, track_ids);

  auto & pattern = song.getPattern(info.getPatternIndex());
  int track_id = track_ids[new_cursor.track];

  bool is_off = ev.getType() == MidiEvent::NOTE_OFF || (ev.getType() == MidiEvent::NOTE_ON && ev.getVelocity() == 0);

  int note_value = 0;
  if (song.getTuning() == Tuning::TET12) note_value = ev.getNote();
  else {
    float best_diff = 1000000.0f, f = Tuner::getFrequency(Tuning::TET12, ev.getNote());
    for (int i = 0; i < 255; i++) {
      float diff = fabsf(f - Tuner::getFrequency(song.getTuning(), i));
      if (diff < best_diff) {
	note_value = i;
	best_diff = diff;
      }
    }
  }

  auto current_delay = info.getCurrentDelay();
  
  int note_column;
  auto it = active_midi_notes.find(ev.getNote());
  if (it != active_midi_notes.end()) {
    note_column = it->second;
  } else {
    active_midi_notes[ev.getNote()] = note_column = active_midi_notes.size();
    cerr << "new note: " << note_column << endl;
  }
  
  if (is_off) {
    active_midi_notes.erase(ev.getNote());
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));

    pattern.setNote(info.getRowIndex(), track_id, note_column, Note(0, 0, current_delay));
  } else if (ev.getType() == MidiEvent::NOTE_ON) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note_value, ev.getVelocity()));

    Note note(note_value, ev.getVelocity(), current_delay);
    pattern.setNote(info.getRowIndex(), track_id, note_column, note);
    row_edited = true;
  } else if (ev.getType() == MidiEvent::NOTE_PRESSURE) {
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, track_id, note_column, note_value, ev.getVelocity()));    

    auto note = pattern.getNote(info.getRowIndex(), track_id, note_column);
    if (!note.isDefined()) note.setDelay(current_delay);
    note.setVelocity(ev.getVelocity());    
    pattern.setNote(info.getRowIndex(), track_id, note_column, note);
    row_edited = true;
  }

#if 0
  if ((!was_playing && !active_midi_notes.empty() && !info.is_playing) ||
      (was_playing && active_midi_notes.empty() && info.is_playing)) {
    bool playing = getController().togglePlaying();
    // setStatus(playing ? "Playing" : "Stopped");
  }
#endif
}

void
PatternEditor::handleLaunchpadPadEvent(LaunchpadPadEvent & ev) {
  if (!launchpad_manager_) return;

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();

  vector<int> track_ids;
  get_root_track_ids(song, track_ids);
  if (track_ids.empty()) return;

  auto device_id = ev.getDeviceIndex();
  auto track_index = launchpad_manager_->assignedTrackIndex(device_id, new_cursor.track);
  if (track_index < 0 || track_index >= static_cast<int>(track_ids.size())) track_index = new_cursor.track;
  int track_id = track_ids[track_index];

  auto note_value = launchpad_manager_->resolveNote(song, device_id, track_id, ev.getX(), ev.getY());
  if (note_value < 0) return; // unused percussion pad (row 7), or an unpitched/degenerate tuning

  auto & pattern = song.getPattern(info.getPatternIndex());
  auto current_delay = info.getCurrentDelay();
  auto & event_queue = getController().getPlaybackEventQueue();

  if (ev.getKind() == LaunchpadPadEvent::PRESS) {
    auto row = info.getRowIndex();

    // Free-slot search (mirrors Pattern::pushNote), deliberately not
    // "map size" the way active_midi_notes assigns columns - that has a
    // latent collision bug on non-LIFO release order, which is the common
    // case for a chordally-played grid controller (see the plan's design
    // decision 3).
    auto & notes = pattern.getNotes(row, track_id);
    int note_column = static_cast<int>(notes.size());
    for (int i = 0; i < static_cast<int>(notes.size()); i++) {
      if (!notes[i].isDefined()) {
	note_column = i;
	break;
      }
    }
    launchpad_manager_->recordActiveNote(device_id, ev.getX(), ev.getY(), {note_column, row, track_id});

    auto velocity = LaunchpadProtocol::getModelInfo(ev.getModel()).velocity_sensitive ?
      static_cast<short>(ev.getVelocity()) : static_cast<short>(0x28); // same default as keyboard entry

    Note note(note_value, velocity, current_delay);
    pattern.setNote(row, track_id, note_column, note);
    row_edited = true;
    launchpad_manager_->setExtraRedrawRow(row); // defensive - see the member's doc comment

    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note_value, velocity));

    // Deliberately NOT auto-advancing here (unlike single-note keyboard
    // entry): a chord is multiple near-simultaneous presses that must all
    // land on the *same* row - advancing per-press would spread a chord
    // across rows the moment any two presses straddle the (asynchronous)
    // MOVE_POSITION round-trip. Advance is deferred to RELEASE, once every
    // currently-held pad has been let go (see below) - matching how
    // Renoise's own "chord mode" treats simultaneously-pressed MIDI notes
    // as one gesture, not N independent steps.
  } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
    auto held_ptr = launchpad_manager_->findActiveNote(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return;
    auto held = *held_ptr;
    launchpad_manager_->clearActiveNote(device_id, ev.getX(), ev.getY());

    // Always silence the live-audition voice.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, held.track_id, held.note_column));

    if (info.isPlaying()) {
      // Live performance recording: write an explicit OFF at the row the
      // transport has since reached, mirroring handleMidiEvent's NOTE_OFF -
      // UNLESS that's still the same row the note itself is on. Per
      // Renoise's own pattern model (a single line can't hold both a note
      // and its own note-off), a release fast enough to land before the
      // row has advanced must not be recorded as an off, or it would
      // instantly erase the note it belongs to.
      auto release_row = info.getRowIndex();
      if (release_row != held.row) {
	pattern.setNote(release_row, held.track_id, held.note_column, Note(0, 0, current_delay));
	row_edited = true;
      }
    } else if (!launchpad_manager_->hasAnyActiveNotes(device_id)) {
      // Step entry: advance once the whole chord gesture has been
      // released on *this* device (not per pad - see the PRESS branch;
      // and scoped to this device, not every connected Launchpad, so one
      // device's chord release doesn't prematurely advance while another
      // device is still mid-chord), so the next tap/chord lands on a
      // fresh row instead of piling onto this one.
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, edit_step_size));
    }
  } else if (ev.getKind() == LaunchpadPadEvent::AFTERTOUCH) {
    // Mini MK3 never emits this (no pressure sensing); defensive check
    // anyway in case a future model reports itself incorrectly.
    if (!LaunchpadProtocol::getModelInfo(ev.getModel()).poly_aftertouch) return;

    auto held_ptr = launchpad_manager_->findActiveNote(device_id, ev.getX(), ev.getY());
    if (!held_ptr) return; // no held note to modulate
    auto & held = *held_ptr;

    // Live modulation always happens, regardless of the write-throttle
    // below - mirrors handleMidiEvent's NOTE_PRESSURE handling exactly.
    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, held.track_id, held.note_column, note_value, ev.getVelocity()));

    // Rate-limit the persisted pattern write: Pattern::setNote already
    // overwrites in place (so "one aftertouch object per column per row" is
    // free), this threshold purely avoids redundant work/redraw churn for
    // a dense pressure stream, not a correctness requirement.
    const int aftertouch_threshold = 4;
    auto delta = ev.getVelocity() - held.last_aftertouch_value;
    if (delta < 0) delta = -delta;
    if (delta < aftertouch_threshold) return;
    held.last_aftertouch_value = ev.getVelocity();

    // While playing, modulate the currently-sounding row (transport has
    // moved on, matching handleMidiEvent); while stopped, modulate the
    // row the note actually landed on (step entry already advanced past
    // it - see the RELEASE branch above for why using the live row would
    // be wrong here too).
    auto target_row = info.isPlaying() ? info.getRowIndex() : held.row;
    auto note = pattern.getNote(target_row, held.track_id, held.note_column);
    if (!note.isDefined()) note.setDelay(current_delay);
    note.setVelocity(static_cast<short>(ev.getVelocity()));
    pattern.setNote(target_row, held.track_id, held.note_column, note);
    row_edited = true;
    launchpad_manager_->setExtraRedrawRow(target_row);
  }
}

bool
PatternEditor::handleLaunchpadDeviceCommand(std::string_view name, int device_id) {
  if (!launchpad_manager_) return false;

  auto & song = getController().getSong();
  vector<int> track_ids;
  get_root_track_ids(song, track_ids);

  if (name == "next-track" || name == "prev-track") {
    if (track_ids.empty()) return true;
    auto delta = name == "next-track" ? 1 : -1;
    auto num_tracks = static_cast<int>(track_ids.size());
    launchpad_manager_->advanceTrack(device_id, delta, new_cursor.track, num_tracks);
    // Also move the shared on-screen cursor, so the button's effect stays
    // visible - see the plan's rationale (byte-for-byte identical to the
    // old single-device behavior; with a second device, each still keeps
    // using its own last-assigned track even after the shared cursor has
    // since moved elsewhere).
    new_cursor.track = launchpad_manager_->assignedTrackIndex(device_id, new_cursor.track);
    new_cursor.col = new_cursor.subcol = 0;
    return true;
  }

  if (name == "octave-up") {
    launchpad_manager_->octaveUp(device_id);
    return true;
  }
  if (name == "octave-down") {
    launchpad_manager_->octaveDown(device_id);
    return true;
  }

  if (name == "toggle-mute" || name == "toggle-solo") {
    if (track_ids.empty()) return true;
    auto track_index = launchpad_manager_->assignedTrackIndex(device_id, current_cursor.track);
    if (track_index < 0 || track_index >= static_cast<int>(track_ids.size())) track_index = current_cursor.track;
    auto track = song.getTrackByInternalId(track_ids[track_index]);
    if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
      auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
      if (name == "toggle-mute") instrument_track.setMuted(!instrument_track.isMuted());
      else instrument_track.setSolo(!instrument_track.isSolo());
      song.incVersion();
    }
    return true;
  }

  return false;
}

bool
PatternEditor::offerInput(const InputEvent & input) {
  if (dispatchCommand(input)) return true;

  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();

  auto all_track_info = getTrackInformation(song);

  vector<int> track_ids;
  get_root_track_ids(song, track_ids);
  auto num_tracks = static_cast<int>(track_ids.size());
  
  auto current_track = song.getTrackByInternalId(track_ids[current_cursor.track]);

  VisibleTrackInfo track_info;
  if (current_track) {
    auto it0 = all_track_info.find(current_track->getInternalId());
    if (it0 != all_track_info.end()) track_info = it0->second;
  }
 
  auto & event_queue = getController().getPlaybackEventQueue();

  if (input.getId() == NCKEY_BUTTON1) {
    
  } else if (input.hasCtrl() && input.hasShift() && !input.hasMeta()) {
    if (input.getId() == 't') {
      // delete track
      return true;
    }
  } else if (input.hasCtrl() && !input.hasMeta()) {
    if (input.getId() == 'r') {
      int track_id;
      auto sample = getController().startRecording();
      if (current_track && current_track->getType() == TrackType::SAMPLE) {
	auto & sample_track = dynamic_cast<SampleTrack&>(*current_track);
	sample_track.setSample(sample);
	track_id = sample_track.getInternalId();
      } else {
	new_cursor.track = track_ids.size();
	auto & track = song.addTrack(make_unique<SampleTrack>(sample));
	track_id = track.getInternalId();
      }
      getController().setRecordingTrackId(track_id);
      song.incVersion();
      return true;
    } else if (input.getId() == 'a') {
      new_cursor.track = new_cursor.col = new_cursor.subcol = 0;
      return true;
    } else if (input.getId() == 'e') {
      new_cursor.track = num_tracks > 1 ? num_tracks - 1 : 0;
      new_cursor.subcol = 0;

      auto it = all_track_info.find(track_ids[new_cursor.track]);
      new_cursor.col = it != all_track_info.end() ? it->second.getColumnCount() - 1: 0;
      return true;
    } else if (input.getId() == 't') {
      song.addTrack(make_unique<InstrumentTrack>(0));
      return true;
    } else if (input.getId() == 'd') {
      // duplicate track
      return true;
    } else if (input.getId() == '+') {
      edit_step_size++;
      return true;
    } else if (input.getId() == '-') {
      if (edit_step_size > 0) edit_step_size--;
      return true;
    } else if (input.getId() == NCKEY_LEFT || input.getId() == NCKEY_RIGHT) {
      auto track = song.getTrackByInternalId(track_ids[current_cursor.track]);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	bool changed = false;
	if (input.getId() == NCKEY_LEFT && instrument_track.getInstrumentId() > 0) {
	  instrument_track.setInstrumentId(instrument_track.getInstrumentId() - 1);
	  changed = true;
	} else {
	  auto & instruments = song.getInstruments();
	  if (input.getId() == NCKEY_RIGHT && instrument_track.getInstrumentId() + 1 < instruments.size()) {
	    instrument_track.setInstrumentId(instrument_track.getInstrumentId() + 1);
	    changed = true;
	  }
	}
	if (changed) {
	  song.incVersion();
	  event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CLEAR_VOICES, instrument_track.getInternalId()));
	}
      }
      return true;
    } else if (input.getId() == 'i') {
      // create new instrument
    } else if (input.getId() == '\\') {
      auto track = song.getTrackByInternalId(track_ids[current_cursor.track]);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	instrument_track.setSolo(!instrument_track.isSolo());
	song.incVersion();
      }
      return true;
    } else {
      return false;
    }
  } else if (input.hasAlt()) {
    if (input.getId() == NCKEY_LEFT) {
      // move selected track to left
      return true;
    } else if (input.getId() == NCKEY_RIGHT) {
      // move selected track to right
      return true;
    }
  } else if (!input.hasMeta()) {
    if (input.getId() == '[') {
      if (current_keyboard_octave > 0) current_keyboard_octave--;
      return true;
    } else if (input.getId() == ']') {
      if (current_keyboard_octave < 9) current_keyboard_octave++;
      return true;
    } else if (input.getId() == NCKEY_LEFT) {
      if (new_cursor.col > 0) {
	new_cursor.col--;
	new_cursor.subcol = 0;
      } else if (new_cursor.track > 0) {
	new_cursor.track--;
	new_cursor.subcol = 0;

	auto it = all_track_info.find(track_ids[new_cursor.track]);
	new_cursor.col = it != all_track_info.end() ? it->second.getColumnCount() - 1 : 0;
      }
      return true;
    } else if (input.getId() == NCKEY_RIGHT) {
      if (new_cursor.col + 1 < track_info.getColumnCount()) {
	new_cursor.col++;
	new_cursor.subcol = 0;
      } else if (new_cursor.track + 1 < num_tracks) {
	new_cursor.track++;
	new_cursor.col = 0;
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_UP || input.getId() == NCKEY_BUTTON4) {
      if (!info.isPlaying()) {
	event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, -1));
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_DOWN || input.getId() == NCKEY_BUTTON5) {
      if (!info.isPlaying()) {
	event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, 1));
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == NCKEY_PGUP) {
      if (!info.isPlaying()) {
	event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, -16));
	new_cursor.subcol = 0;
      }
      return true;    
    } else if (input.getId() == NCKEY_PGDOWN) { // scrollwheel down
      if (!info.isPlaying()) {
	event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, 16));
	new_cursor.subcol = 0;
      }
      return true;
    } else if (input.getId() == '\\') {
      auto track = song.getTrackByInternalId(track_ids[current_cursor.track]);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL)) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	instrument_track.setMuted(!instrument_track.isMuted());
	song.incVersion();
      }
      return true;
    } else if (input.getId() == '\t') {
      if (track_info.isEffectColumn(new_cursor.col)) { // effect
	new_cursor.subcol = (new_cursor.subcol + 1) % 4;
      } else if (!track_info.isNoteColumn(new_cursor.col)) {
	new_cursor.subcol = (new_cursor.subcol + 1) % 2;
      }
      return true;
    } else if (input.getId() == NCKEY_INS) {
      auto & pattern = song.getPattern(info.getPatternIndex());
      int track_id = track_ids[new_cursor.track];
      pattern.insertRow(info.getRowIndex(), track_id);
      song.incVersion();
      return true;
    } else {
      auto & pattern = song.getPattern(info.getPatternIndex());
      int track_id = track_ids[new_cursor.track];
      bool is_hex = (input.getId() >= 'a' && input.getId() <= 'z') || (input.getId() >= '0' && input.getId() <= '9');
      auto column_type = track_info.getColumnType(new_cursor.col);
    
      if (column_type == ColumnType::EFFECT) {
	if (is_hex || input.getId() == '-') {
	  auto command = pattern.getCommand(info.getRowIndex(), track_id);
	  command.updateData(new_cursor.subcol, toupper(input.getId()));
	  pattern.setCommand(info.getRowIndex(), track_id, command);
	  row_edited = true;

	  if (new_cursor.subcol + 1 < 4) {
	    new_cursor.subcol++;
	  } else if (new_cursor.track + 1 < num_tracks) {
	    new_cursor.track++;
	    new_cursor.col = 0;
	    new_cursor.subcol = 0;
	  }
	  return true;
	}
      } else if (column_type == ColumnType::VELOCITY || column_type == ColumnType::DELAY) {
	bool is_hex_digit = (input.getId() >= 'a' && input.getId() <= 'f') || (input.getId() >= '0' && input.getId() <= '9');
	if (is_hex_digit) {
	  int input_value = input.getId() >= '0' && input.getId() <= '9' ? input.getId() - '0' : input.getId() - 'a' + 10;
	  auto & notes = pattern.getNotes(info.getRowIndex(), track_id);
	  auto note_column = track_info.getNoteNumber(new_cursor.col);
	  Note note;
	  if (note_column < notes.size()) note = notes[note_column];
	  int current_value = column_type == ColumnType::VELOCITY ? note.getVelocity() : note.getDelay();
	  if (new_cursor.subcol == 0) current_value = (input_value << 4) | (current_value & 0x0f);
	  else current_value = (current_value & 0xf0) | input_value;
	  if (column_type == ColumnType::VELOCITY) note.setVelocity(current_value);
	  else note.setDelay(current_value);
	  pattern.setNote(info.getRowIndex(), track_id, note_column, note);
	  row_edited = true;
	  if (new_cursor.subcol == 0) {
	    new_cursor.subcol++;
	  } else if (new_cursor.col + 1 < track_info.getColumnCount()) {
	    new_cursor.col++;
	    new_cursor.subcol = 0;
	  } else if (new_cursor.track + 1 < num_tracks) {
	    new_cursor.track++;
	    new_cursor.col = 0;
	    new_cursor.subcol = 0;
	  }
	  return true;
	}
      } else {
	bool is_off = input.getId() == 'a';
	bool is_delete = input.getId() == NCKEY_DEL || input.getId() == NCKEY_BACKSPACE;
	auto note_column = track_info.getNoteNumber(new_cursor.col);
	auto current_delay = info.getCurrentDelay();
	
	int midi_note = -1;
	if (!is_off) {
	  auto track = song.getTrackByInternalId(track_id);
	  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	  midi_note = input.toMidiNote(current_keyboard_octave, tuning);
	}
      
	if (is_delete || midi_note >= 0 || is_off) {
	  if (is_delete) {
	    pattern.deleteNote(info.getRowIndex(), track_id, note_column);
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));
	  } else if (is_off) {
	    pattern.setNote(info.getRowIndex(), track_id, note_column, Note(0, 0, current_delay)); 
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));
	  } else {
	    Note note(midi_note, 0x28, current_delay);
	    
	    if (input.hasShift()) {
	      note_column = pattern.pushNote(info.getRowIndex(), track_id, note);
	    } else {
	      pattern.setNote(info.getRowIndex(), track_id, note_column, note); 
	    }
	  
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note.getValue(), note.getVelocity()));
	  }

	  row_edited = true;
	
	  if (!info.isPlaying()) {
	    int n = 0;
	    if (input.getId() != NCKEY_DEL && input.getId() != NCKEY_BACKSPACE) n = 1;
	    if (n) {
	      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, n * edit_step_size));
	    }
	  }
      
	  return true;
	}
      }
    }
  }
  
  return false;
}

void
PatternEditor::renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & all_track_info) {
  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto & pattern = song.getPattern(info.getPatternIndex());

  auto [rows, cols] = getDim();

  unordered_map<int, Track *> track_parents;
  for (auto & track : song.getTracks()) {
    get_track_parents(*track, nullptr, track_parents);
  }

  auto heading_height = song.getTrackDepth() + 1;

  string padding(cols, ' ');
  
  setBgColor(styles.window_bg_color);
  for (auto i = 0; i < heading_height; i++) {
    putstr(i, 0, padding);
  }
  
  auto & instruments = song.getInstruments();
  
  for (auto level = 0; level < heading_height - 1; level++) {
    vector<Track *> tracks;
    vector<int> track_widths;

    for (auto i = 0; i < static_cast<int>(track_ids.size()); i++) {
      int track_id = track_ids[i];
      auto track = song.getTrackByInternalId(track_id);
      for (auto k = 0; k < level && track; k++) {
	auto it = track_parents.find(track->getInternalId());
	if (it != track_parents.end()) track = it->second;
	else track = nullptr;	
      }
      auto it = all_track_info.find(track_id);
      auto w = it != all_track_info.end() ? it->second.getTrackWidth() : 0;

      if (!tracks.empty() && tracks.back() == track) {
	track_widths.back() += w;
      } else {
	tracks.push_back(track);
	track_widths.push_back(w);
      }
    }
    
    auto current_pos = 5;
    for (auto i = 0; i < static_cast<int>(tracks.size()); i++) {
      if (i < current_scroll_track) continue;
      if (current_pos >= cols) break;
      
      auto track = tracks[i];
      auto actual_width = track_widths[i];

      if (track) {
	if (level == 0) {
	  setFgColor(0x00, 0x00, 0x00);
	  setBgColor(0xf0, 0x80, 0x10);

	  auto text_width = actual_width - 3;

	  bool is_solo = false, is_muted = false;
	  string instrument_name;
	  if (track->getType() == TrackType::SAMPLE) {
	    instrument_name = "Sample";
	  } else if (track->getType() == TrackType::INSTRUMENT_CONTROL || track->getType() == TrackType::PERCUSSION_CONTROL) {
	    auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
	    if (instrument_track.getInstrumentId() >= 0 && instrument_track.getInstrumentId() < instruments.size()) {
	      instrument_name = instruments[instrument_track.getInstrumentId()]->getName();
	      is_solo = instrument_track.isSolo();
	      is_muted = instrument_track.isMuted();
	    }
	  }
	  auto name = !track->getName().empty() ? track->getName() : (!track->getId().empty() ? "Trk " + track->getId() : format("Trk {:02d}", track->getInternalId()));
	  if (name.size() > text_width) name.erase(text_width);
	  else {
	    while (name.size() < text_width) name += ' ';
	  }
	  putstr(heading_height - 2 - level, current_pos, name);
	  putstr(heading_height - 2 - level, current_pos + text_width + 2, "│");
	  if (is_muted) setFgColor(0x00, 0x00, 0x00);
	  else setFgColor(0xe0, 0x70, 0x08);
	  putstr(heading_height - 2 - level, current_pos + text_width, "M");
	  if (is_solo) setFgColor(0x00, 0x00, 0x00);
	  else setFgColor(0xe0, 0x70, 0x08);
	  putstr(heading_height - 2 - level, current_pos + text_width + 1, "S");
	  
	  setFgColor(0xf0, 0xf0, 0xf0);
	  setBgColor(styles.window_bg_color);
	  	  
	  if (instrument_name.size() > actual_width - 1) instrument_name.erase(actual_width - 1);
	  putstr(heading_height - 2 - level + 1, current_pos, instrument_name);
	} else {	  
	  std::string name = track->getElementName();
	  auto & track_info = info.getTrackInfo(track->getInternalId());
	  
	  if (name.size() > actual_width - 4) name.erase(actual_width - 4);
	  else {
	    while (name.size() < actual_width - 4) name += ' ';
	  }
	  name += "│";

	  setBgColor(0x50, 0x50, 0x60);

	  if (track_info.isClipping()) {
	    setFgColor(0xe0, 0x10, 0x40);
	  } else {
	    setFgColor(0x10, 0xe0, 0x40);
	  }
	  
	  putstr(heading_height - 2 - level, current_pos, track_info.isActive() ? " • " : "   ");
	  
	  setFgColor(0x00, 0x00, 0x00);
	  putstr(heading_height - 2 - level, current_pos + 3, name);
	}
      }
      
      current_pos += actual_width;
    }
  }
}

void
PatternEditor::renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & all_track_info, int display_row, bool highlight, const SelectionBounds & sel_bounds) {
  auto [rows, cols] = getDim();

  if (display_row >= rows - heading_height) {
    return;
  }
    
  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto [ pattern_idx, pattern_row ] = song.normalizePosition(info.getPatternIndex(), display_row + current_scroll_row);  
  bool is_neighboring_pattern = info.getPatternIndex() != pattern_idx;
  auto & pattern = song.getPattern(pattern_idx);

  display_row += heading_height;
          
  string padding(cols, ' ');
    
  setBgColor(styles.window_bg_color);
  putstr(display_row, 0, padding);
    
  auto current_pos = 0;
  for (int i = -1; i < static_cast<int>(track_ids.size()); i++) {
    if (i >= 0 && i < current_scroll_track) continue;
    if (current_pos >= cols) break;
    
    UIColor fg, bg, cell_fg, cell_bg;
      
    if (highlight) {
      fg = UIColor("#80c080");
      bg = UIColor("#80a080");
    } else if (pattern_row % 4 == 0) {
      fg = styles.window_accent_fg_color;
      bg = styles.window_accent_bg_color;
    } else {
      fg = styles.window_fg_color;
      bg = styles.window_bg_color;
    }

    if (is_neighboring_pattern) {
      UIColor black;
      bg = bg.blend(0.75f, black);
      fg = fg.blend(0.75f, black);
    }

    // There's always an effective region to highlight, even with no mark
    // set - it degenerates to just the note under the cursor (see
    // getEffectiveSelectionBounds, computed once per render() call).
    bool row_track_in_selection = pattern_idx == info.getPatternIndex() &&
      i >= sel_bounds.track_lo && i <= sel_bounds.track_hi &&
      pattern_row >= sel_bounds.row_lo && pattern_row <= sel_bounds.row_hi;
    // A single-track region is scoped to specific note columns (see
    // set-mark/kill-region) - highlight per-column inside the loop below
    // instead of the whole track uniformly.
    bool column_scoped_selection = row_track_in_selection && sel_bounds.column_scoped;
    bool in_selection = row_track_in_selection && !column_scoped_selection;
    if (in_selection) {
      fg = styles.highlight_fg_color;
      bg = styles.highlight_bg_color;
    }

    if (i == -1) {
      setFgColor(fg);
      setBgColor(bg);
	
      putstr(display_row, current_pos, format(" {:02x} ", pattern_row));
	
      setFgColor(styles.window_border_color);
      setBgColor(styles.window_bg_color);
	
      putstr(display_row, current_pos + 4, "│");
	
      current_pos += 5;
    } else {
      auto track_id = track_ids[i];
      auto & notes = pattern.getNotes(pattern_row, track_id);
      auto & command = pattern.getCommand(pattern_row, track_id);
      VisibleTrackInfo track_info;
      auto it = all_track_info.find(track_id);
      if (it != all_track_info.end()) track_info = it->second;
      auto track = song.getTrackByInternalId(track_id);

      for (auto k = 0; k < track_info.getColumnCount(); k++) {
	if (k != 0) {
	  putstr(display_row, current_pos++, " ");
	}
	// column_highlighted used to be its own distinct "here's the cursor"
	// color layer; now the effective region always covers the cursor's
	// position too, so it's folded into column_selected below - kept
	// only to (a) still indicate the effect column specifically, since
	// it isn't part of any note column and so is otherwise excluded from
	// the note-range check, and (b) drive the active-character underline
	// for numeric columns further down, unchanged.
	bool column_highlighted = highlight && current_cursor.isHighlighted(i, k);
	// Per-column override of the track-level fg/bg for a single-track
	// (column-scoped) region.
	bool column_selected = column_scoped_selection &&
	  ((!track_info.isEffectColumn(k) &&
	    track_info.getNoteNumber(k) >= sel_bounds.note_lo && track_info.getNoteNumber(k) <= sel_bounds.note_hi) ||
	   (track_info.isEffectColumn(k) && column_highlighted));
	UIColor cur_fg = column_selected ? styles.highlight_fg_color : fg;
	UIColor cur_bg = column_selected ? styles.highlight_bg_color : bg;

	setFgColor(styles.window_border_color);
	setBgColor(cur_bg);
	auto column_type = track_info.getColumnType(k);
	if (track && track->getType() == TrackType::SAMPLE) {
	  cell_fg = cur_fg;
	  cell_bg = cur_bg;
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);

	  if (k < notes.size() && notes[k].isDefined()) {
	    auto & note = notes[k];
	    putstr(display_row, current_pos, "xxxxxx");
	  } else {
	    putstr(display_row, current_pos, "      ");
	  }
	} else if (column_type == ColumnType::EFFECT) {
	  cell_fg = command.isDefined() ? styles.command_column_color : cur_fg;
	  cell_bg = cur_bg;
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  auto s = to_string(command);
	  putstr(display_row, current_pos, s);
	  if (column_highlighted) {
	    setUnderline(true);
	    putstr(display_row, current_pos + new_cursor.subcol, s[new_cursor.subcol]);
	    setUnderline(false);
	  }
	  current_pos += 4;
	} else if (column_type == ColumnType::NOTE) {
	  auto l = track_info.getNoteNumber(k);
	  auto note = l < notes.size() ? notes[l] : Note();

	  cell_fg = cur_fg;
	  cell_bg = cur_bg;
	  if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  auto tuning = track && track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	  putstr(display_row, current_pos, note.toString(tuning));
	  current_pos += 3;
	} else if (column_type == ColumnType::VELOCITY || column_type == ColumnType::DELAY) {
	  auto l = track_info.getNoteNumber(k);
	  auto note = l < notes.size() ? notes[l] : Note();
	  string s;
	  if (note.isDefined()) {
	    if (column_type == ColumnType::VELOCITY && note.isOff()) {
	      s = "  ";
	    } else {
	      s = format("{:02x}", column_type == ColumnType::VELOCITY ? note.getVelocity() : note.getDelay());
	    }
	  } else {
	    s = "--";
	  }

	  // The bright velocity/delay colors are tuned for contrast against the
	  // normal dark row background; inside the (bright) effective-region
	  // highlight they'd be nearly unreadable, so use the region's own
	  // (dark) foreground there instead - same idea as the note column.
	  cell_fg = column_selected ? cur_fg : (column_type == ColumnType::VELOCITY ? UIColor("#bfa426") : UIColor("#42c1ea"));
	  cell_bg = cur_bg;
	  if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);

	  putstr(display_row, current_pos, s);
	  if (column_highlighted) {
	    setUnderline(true);
	    putstr(display_row, current_pos + new_cursor.subcol, s[new_cursor.subcol]);
	    setUnderline(false);
	  }
	  current_pos += 2;
	}
      }

      setFgColor(styles.window_border_color);
      setBgColor(bg);
      putstr(display_row, current_pos, "│");
      current_pos++;
    }
  }

  if (current_pos + 2 < cols) {
    auto & annotation = pattern.getAnnotation(pattern_row);
    if (!annotation.empty()) {
      setFgColor("#e03030");
      setBgColor("#702020");
      putstr(display_row, current_pos + 2, annotation);
    }
  }
}
