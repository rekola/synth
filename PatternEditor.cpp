#include "PatternEditor.h"

#include "UIInput.h"
#include "Synth.h"
#include "Controller.h"
#include "StyleProvider.h"

#include <string>
#include <fmt/core.h>

using namespace std;
using namespace fmt;

PatternEditor::PatternEditor(UIPlane & parent) : UIElement(parent) {
  // getPlane().setScrolling(true);  
}

bool
PatternEditor::render(const StyleProvider & styles, bool refresh) {  
  bool render_all = refresh;
  auto & synth = getController().getSynth();
  size_t score_pattern = synth.getPatternPosition();
  size_t score_playing_row = synth.getTrackPosition();
  auto & song = getController().getSong();
  auto & current_pattern = song.getPattern(score_pattern);
  auto & mastertrack = song.getMasterTrack();
  auto & tracks = mastertrack.getChildren();

  auto track_widths = current_pattern.getTrackWidths();
  size_t score_total_columns = 0;
  for (auto w : track_widths) score_total_columns += w;

  auto [rows, cols] = getDim();

  size_t new_scroll_row = current_scroll_row;
  if (score_playing_row < new_scroll_row) {
    new_scroll_row = score_playing_row;
  } else if (score_playing_row >= new_scroll_row + rows - 4) {
    new_scroll_row = score_playing_row - (rows - 4) + 1;
  }

  size_t new_scroll_col = current_scroll_col;
  if (new_score_cursor_col < new_scroll_col) {
    new_scroll_col = new_score_cursor_col;
  } else {
    while ( 0 ) {
      size_t pos = 6;
      for (size_t i = new_scroll_col; i < tracks.size() && i <= new_score_cursor_col; i++) {
	auto note_columns = track_widths[i];
	size_t actual_width = (note_columns == 0 ? 1 : note_columns) * 7;
	pos += actual_width;
      }
      if (pos >= (size_t)cols) {
	new_scroll_col++;
      } else {
	break;
      }
    }
  }

  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_scroll_row != current_scroll_row ||
      new_scroll_col != current_scroll_col
      ) {
    render_all = true;
  }
  
  // bool cursor_row_changed = new_score_cursor_row != current_score_cursor_row;
  bool cursor_col_changed = new_score_cursor_col != current_score_cursor_col;
  
  // size_t old_cursor_row = current_score_cursor_row;
  
  // current_score_cursor_row = new_score_cursor_row;
  current_score_cursor_col = new_score_cursor_col;

  bool need_redraw = false;
  if (render_all) {
    current_scroll_row = new_scroll_row;
    current_scroll_col = new_scroll_col;
    
    erase();
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    fill();
    getPlane().drawBorder();
    
    renderHeading(styles, track_widths);
    for (size_t row = 0; row < current_pattern.getNumRows(); row++) {
      renderRow(styles, track_widths, row, row == score_playing_row);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderRow(styles, track_widths, current_score_playing_row, false);
    renderRow(styles, track_widths, score_playing_row, true);
    need_redraw = true;
  } else if (cursor_col_changed || row_edited) {
    renderRow(styles, track_widths, score_playing_row, true);
    need_redraw = true;
  }

  Tuning new_tuning = current_pattern.getTuning() != Tuning::INHERIT ? current_pattern.getTuning() : song.getTuning();
  int new_tempo = song.getTempo();
  int new_key = current_pattern.getKey() ? current_pattern.getKey() : song.getKey();
  
  if (render_all || edit_step_size != new_edit_step_size || new_tuning != current_tuning || new_tempo != current_tempo || new_key != current_key) {
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);

    std::string tuning;
    if (new_tuning == Tuning::TET12) {
      tuning = "12-TET";
    } else if (new_tuning == Tuning::TET31) {
      tuning = "31-TET";
    } else {
      tuning = "error";
    }
    
    putstr(rows - 1, cols - 16, format("{:2d} {} {}", new_edit_step_size, tuning, new_tempo));
    
    edit_step_size = new_edit_step_size;
    current_tuning = new_tuning;
    current_tempo = new_tempo;
    current_key = new_key;
    
    need_redraw = true;
  }
  
  current_score_pattern = score_pattern;
  current_score_playing_row = score_playing_row;
  current_score_total_columns = score_total_columns;
  current_song_version = song.getVersion();
  row_edited = false;
  
  return need_redraw;
}

bool
PatternEditor::offerInput(const UIInput & input) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & mastertrack = song.getMasterTrack();
  auto & tracks = mastertrack.getChildren();
  size_t num_columns = tracks.size();

  if (input.hasCtrl()) {
    if (input.getId() == 'r') {
      auto sample = getController().startRecording();
      auto & current_track = tracks[current_score_cursor_col];
      if (current_track.getType() == Track::SAMPLE) {
	current_track.setSample(sample);
      } else {
	new_score_cursor_col = tracks.size();
	auto & track = mastertrack.addChild(Track::SAMPLE);
	track.setSample(sample);
      }
      song.incVersion();
    } else if (input.getId() == 'e') {
      getController().stopRecording();
    } else if (input.getId() == 'a' || input.getId() == 'A') {
      new_score_cursor_col = 0;
      return true;
    } else if (input.getId() == 'e' || input.getId() == 'E') {
      new_score_cursor_col = num_columns > 1 ? num_columns - 1 : 0;
      return true;
    } else if (input.getId() == 't' || input.getId() == 'T') {
      int instrument_id = 0; // pattern.getTracks().back().getInstrumentId();
      auto & track = mastertrack.addChild();
      track.setInstrumentId(instrument_id); // + 1);
      song.incVersion();
    } else if (input.getId() == 'g' || input.getId() == 'G') {
      // create group
      return true;
    } else if (input.getId() == 'd' || input.getId() == 'D') {
      // duplicate track
      return true;
    } else if (input.getId() == '+') {
      edit_step_size++;
    } else if (input.getId() == '-') {
      if (edit_step_size > 0) edit_step_size--;
    } else if (input.hasAlt() && input.getId() == NCKEY_LEFT) {
      // move selected track to left
      return true;
    } else if (input.hasAlt() && input.getId() == NCKEY_RIGHT) {
      // move selected track to right
      return true;
    } else if (input.getId() == NCKEY_LEFT || input.getId() == 'p') {
      auto & track = tracks[current_score_cursor_col];
      if (track.getInstrumentId() > 0) {
	track.setInstrumentId(track.getInstrumentId() - 1);
	song.incVersion();
      }
      return true;
    } else if (input.getId() == NCKEY_RIGHT || input.getId() == 'i' || input.getId() == 'i' || input.getId() == 'o') {
      auto & track = tracks[current_score_cursor_col];
      auto & instruments = song.getInstruments();
      if (track.getInstrumentId() + 1 < instruments.size()) {
	track.setInstrumentId(track.getInstrumentId() + 1);
	song.incVersion();
      }
      return true;
    } else if (input.getId() == '\\') {
      tracks[current_score_cursor_col].setSolo(true);
    } else if (input.hasShift()) {
      if (input.getId() == 't' || input.getId() == 'T') {
	// delete track
	return true;
      }
    } else {
      return false;
    }
  } else if (input.getId() == NCKEY_LEFT) {
    if (new_score_cursor_col > 0) {
      new_score_cursor_col--;
    }
    return true;
  } else if (input.getId() == NCKEY_RIGHT) {
    if (new_score_cursor_col + 1 < num_columns) {
      new_score_cursor_col++;
    }
    return true;
  } else if (input.getId() == NCKEY_UP || input.getId() == NCKEY_BUTTON4) {
    if (!synth.isPlaying()) synth.moveBackwards(song);
    return true;
  } else if (input.getId() == NCKEY_DOWN || input.getId() == NCKEY_BUTTON5) {
    if (!synth.isPlaying()) synth.moveForward(song);
    return true;
  } else if (input.getId() == '\t') {
    // next note column
    return true;
  } else if (input.getId() == NCKEY_PGUP) {
    if (!synth.isPlaying()) synth.moveBackwards(song, 16);
    return true;
  } else if (input.getId() == NCKEY_PGDOWN) { // scrollwheel down
    if (!synth.isPlaying()) synth.moveForward(song, 16);
    return true;
  } else if (input.getId() == '\\') {
    tracks[current_score_cursor_col].setMute(true);   
  } else {
    auto & pattern = song.getPattern(synth.getPatternPosition());
    Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
    int midi_note = input.toMidiNote(tuning);
    if (midi_note != -1) {
      if (midi_note == 0) {
	pattern.deleteNote(current_score_cursor_col, synth.getTrackPosition());
      } else {
	Note note(midi_note);
	auto & track = tracks[current_score_cursor_col];
	auto & instrument = song.getInstrument(track.getInstrumentId());

	size_t note_column = 0;
	if (input.hasShift()) {
	  note_column = pattern.pushNote(current_score_cursor_col, synth.getTrackPosition(), note);
	} else {
	  pattern.setNote(current_score_cursor_col, synth.getTrackPosition(), 0, note);
	}

	Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
	track.playNote(tuning, note, instrument, note_column);
	row_edited = true;
      }
      
      if (!synth.isPlaying()) {
	if (input.getId() == NCKEY_BACKSPACE) synth.moveBackwards(song, edit_step_size);
	else if (input.getId() != NCKEY_DEL) synth.moveForward(song, edit_step_size);
      }
      
      return true;
    }
  }
  
  return false;
}

void
PatternEditor::renderHeading(const StyleProvider & styles, const std::vector<size_t> & track_widths) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & pattern = song.getPattern(synth.getPatternPosition());

  auto [rows, cols] = getDim();
  
  string padding;
  for (size_t i = 1; i < cols - 1; i++) padding += ' ';

  setBgColor(styles.window_bg_color);
  putstr(1, 1, padding);
  putstr(2, 1, padding);

  auto & mastertrack = song.getMasterTrack();
  auto & tracks = mastertrack.getChildren();
  auto & instruments = song.getInstruments();

  size_t current_pos = 6;
  for (size_t i = 0; i < tracks.size(); i++) {
    if (i < current_scroll_col) continue;
    if (current_pos >= cols) break;

    auto & track = tracks[i];
    auto note_columns = i < track_widths.size() ? track_widths[i] : 0;
    size_t actual_width = (note_columns == 0 ? 1 : note_columns) * 7;
    
    setFgColor(0x00, 0x00, 0x00);
    setBgColor(0xf0, 0x80, 0x10);
    
    string name = format("Trk {:02d}", i);
    if (name.size() > actual_width - 1) name.erase(actual_width - 1);
    else {
      while (name.size() < actual_width - 1) name += ' ';
    }
    name += "│";
    putstr(1, current_pos, name);

    setFgColor(0xf0, 0xf0, 0xf0);
    setBgColor(styles.window_bg_color);

    string instrument_name;
    if (track.getType() == Track::SAMPLE) {
      instrument_name = "Sample";
    } else if (track.getInstrumentId() >= 0 && track.getInstrumentId() < instruments.size()) {
      instrument_name = instruments[track.getInstrumentId()]->getName();
    }
    putstr(2, current_pos, instrument_name);
    
    current_pos += actual_width;
  }
}

void
PatternEditor::renderRow(const StyleProvider & styles, const std::vector<size_t> & track_widths, size_t row, bool highlight) {
  auto [rows, cols] = getDim();

  if (row >= current_scroll_row && row < current_scroll_row + rows - 4) {
    auto & song = getController().getSong();
    auto & synth = getController().getSynth();
    auto & pattern = song.getPattern(synth.getPatternPosition());
    
    string padding;
    for (size_t i = 1; i < cols - 1; i++) padding += ' ';
    
    setBgColor(styles.window_bg_color);
    putstr(3 + row - current_scroll_row, 1, padding);

    auto & mastertrack = song.getMasterTrack();
    auto & tracks = mastertrack.getChildren();
    
    size_t current_pos = 1;
    for (int i = -1; i < (int)tracks.size(); i++) {
      if (i >= 0 && (size_t)i < current_scroll_col) continue;
      if (current_pos >= cols) break;
          
      UIColor fg, bg, cell_fg, cell_bg;
      
      if (highlight) {
	fg = UIColor("#80c080");
	bg = UIColor("#80a080");
      } else if (row % 4 == 0) {
	fg = styles.window_accent_fg_color;
	bg = styles.window_accent_bg_color;
      } else {
	fg = styles.window_fg_color;
	bg = styles.window_bg_color;
      }
      
      if (highlight && i == (int)current_score_cursor_col) {
	cell_fg = UIColor("#000000");
	cell_bg = UIColor("#a0ffa0");
      } else {
	cell_fg = fg;
	cell_bg = bg;
      }
      
      if (i == -1) {
	setFgColor(cell_fg);
	setBgColor(cell_bg);
	
	putstr(3 + row - current_scroll_row, current_pos, format(" {:02x} ", row));
	
	setFgColor(styles.window_border_color);
	setBgColor(styles.window_bg_color);
	
	putstr(3 + row - current_scroll_row, current_pos + 4, "│");
	
	current_pos += 5;
      } else {
	auto & track = tracks[i];
	auto notes = pattern.getNotes(i, row);
	auto note_columns = i < track_widths.size() ? track_widths[i] : 0;
	if (note_columns == 0) note_columns = 1;

	for (size_t k = 0; k < note_columns; k++) {
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);

	  if (track.getType() == Track::SAMPLE) {
	    putstr(3 + row - current_scroll_row, current_pos, "      ");
	  } else {
	    string s;
	    if (k < notes.size() && notes[k].isDefined()) {
	      auto & note = notes[k];
	      s = note.toString(song.getTuning()) + format(" {:02x}", note.getVelocity());
	    } else {
	      s = "... ..";
	    }	    
	    putstr(3 + row - current_scroll_row, current_pos, s);
	  }
	  
	  setFgColor(styles.window_border_color);
	  setBgColor(bg);
	    
	  putstr(3 + row - current_scroll_row, current_pos + 6, "│");
	  
	  current_pos += 7;
	}
      }
    }

    if (current_pos + 2 < (size_t)cols) {
      auto & annotation = pattern.getAnnotation(row);
      if (!annotation.empty()) {
	setFgColor("#e03030");
	setBgColor("#702020");
	putstr(3 + row - current_scroll_row, current_pos + 2, annotation);
      }
    }
  }
}
