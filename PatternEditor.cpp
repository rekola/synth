#include "PatternEditor.h"

#include "InputEvent.h"
#include "SongState.h"
#include "Controller.h"
#include "StyleProvider.h"
#include "Tuner.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"
#include "MidiEvent.h"
#include "PlaybackControlEvent.h"

#include <string>
#include <fmt/core.h>

#include <iostream>

using namespace std;
using namespace fmt;

PatternEditor::PatternEditor(UIPlane & parent) : UIElement(parent) {
  // getPlane().setScrolling(true);  
}

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

bool
PatternEditor::render(const StyleProvider & styles, bool refresh) {
  bool render_all = refresh;
  auto & info = getController().getPlaybackInfo();
  auto score_pattern = info.getPatternIndex();
  auto score_playing_row = info.getRowIndex();
  auto & song = getController().getSong();
  
  auto track_info = getTrackInformation(song);

  auto [rows, cols] = getDim();
  auto heading_height = song.getTrackDepth() + 1;
  
  vector<int> track_ids;
  get_root_track_ids(song, track_ids);
			      
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

  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_scroll_row != current_scroll_row ||
      new_scroll_track != current_scroll_track
      ) {
    render_all = true;
  }
  
  bool cursor_changed = new_cursor.track != current_cursor.track || new_cursor.col != current_cursor.col || new_cursor.subcol != current_cursor.subcol;
  
  current_cursor.track = new_cursor.track;
  current_cursor.col = new_cursor.col;
  current_cursor.subcol = new_cursor.subcol;
  
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
      renderRow(styles, heading_height, track_ids, track_info, row, (row + current_scroll_row) == score_playing_row);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderHeading(styles, track_ids, track_info);
    renderRow(styles, heading_height, track_ids, track_info, current_score_playing_row - current_scroll_row, false);
    renderRow(styles, heading_height, track_ids, track_info, score_playing_row - current_scroll_row, true);
    need_redraw = true;
  } else if (cursor_changed || row_edited) {
    renderRow(styles, heading_height, track_ids, track_info, score_playing_row - current_scroll_row, true);
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
    Tuner tuner;
    float best_diff = 1000000.0f, f = tuner.getFrequency(Tuning::TET12, song.getKey(), ev.getNote());
    for (int i = 0; i < 255; i++) {
      float diff = fabsf(f - tuner.getFrequency(song.getTuning(), song.getKey(), i));
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

bool
PatternEditor::offerInput(const InputEvent & input) {
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
    if (input.getId() == NCKEY_UP) {
      auto & pattern = song.getPattern(info.getPatternIndex());
      pattern.transposeUp();
      song.incVersion();
    } else if (input.getId() == NCKEY_DOWN) {
      auto & pattern = song.getPattern(info.getPatternIndex());
      pattern.transposeDown();
      song.incVersion();
    } else if (input.getId() == 't') {
      // delete track
      return true;
    }
  } else if (input.hasCtrl() && !input.hasMeta()) {
    if (input.getId() == ' ') {
      // setStatus("marking");
    } else if (input.getId() == 'r') {
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
    } else if (0 && (input.getId() == 'e')) {
      getController().stopRecording();
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
    } else if (input.getId() == 'g') {
      // create group
      return true;
    } else if (input.getId() == 'd') {
      // duplicate track
      return true;
    } else if (input.getId() == '+') {
      edit_step_size++;
    } else if (input.getId() == '-') {
      if (edit_step_size > 0) edit_step_size--;
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
    } else if (input.getId() == ']') {
      if (current_keyboard_octave < 9) current_keyboard_octave++;
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
    } else if (input.getId() == '\t') {
      if (track_info.isEffectColumn(new_cursor.col)) { // effect
	new_cursor.subcol = (new_cursor.subcol + 1) % 4;
      } else if (!track_info.isNoteColumn(new_cursor.col)) {
	new_cursor.subcol = (new_cursor.subcol + 1) % 2;
      }
    } else if (input.getId() == NCKEY_INS) {
      auto & pattern = song.getPattern(info.getPatternIndex());
      int track_id = track_ids[new_cursor.track];
      pattern.insertRow(info.getRowIndex(), track_id);
      song.incVersion();
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
	}
	return true;
      } else if (column_type == ColumnType::VELOCITY || column_type == ColumnType::DELAY) {
	if (is_hex) {
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
	  } else {
	    new_cursor.col++;
	    new_cursor.subcol = 0;
	  }
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
	    if (input.getId() == NCKEY_BACKSPACE) n = -1;
	    else if (input.getId() != NCKEY_DEL) n = 1;
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
PatternEditor::renderRow(const StyleProvider & styles, int heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & all_track_info, int display_row, bool highlight) {
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
	bool column_highlighted = highlight && current_cursor.isHighlighted(i, k);
	if (column_highlighted) {
	  cell_fg = UIColor("#000000");
	  cell_bg = UIColor("#a0ffa0");
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	} else {
	  setFgColor(styles.window_border_color);
	  setBgColor(bg);
	}
	auto column_type = track_info.getColumnType(k);
	if (track && track->getType() == TrackType::SAMPLE) {
	  if (!column_highlighted) {
	    cell_fg = fg;
	    cell_bg = bg;
	    setFgColor(cell_fg);
	    setBgColor(cell_bg);
	  }
	  
	  if (k < notes.size() && notes[k].isDefined()) {
	    auto & note = notes[k];
	    putstr(display_row, current_pos, "xxxxxx");
	  } else {
	    putstr(display_row, current_pos, "      ");	    
	  }
	} else if (column_type == ColumnType::EFFECT) {
	  if (!column_highlighted && command.isDefined()) {
	    cell_fg = styles.command_column_color;
	    cell_bg = bg;
	    setFgColor(cell_fg);
	    setBgColor(cell_bg);
	  }
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

	  if (!column_highlighted) {
	    cell_fg = fg;
	    cell_bg = bg;
	    if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	    setFgColor(cell_fg);
	    setBgColor(cell_bg);
	  }
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

	  if (!column_highlighted) {
	    cell_fg = column_type == ColumnType::VELOCITY ? UIColor("#bfa426") : UIColor("#42c1ea");
	    cell_bg = bg;
	    if (!note.isDefined()) cell_fg = cell_fg.blend(0.5f, cell_bg);
	    setFgColor(cell_fg);
	    setBgColor(cell_bg);
	  }

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
