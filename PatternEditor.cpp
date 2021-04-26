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

  auto [rows, cols] = getDim();

  size_t new_score_scroll_row = current_score_scroll_row;
  if (score_playing_row < new_score_scroll_row) {
    new_score_scroll_row = score_playing_row;
  } else if (score_playing_row >= new_score_scroll_row + rows - 3) {
    new_score_scroll_row = score_playing_row - (rows - 3) + 1;
  }

  auto track_widths = current_pattern.getTrackWidths();
  size_t score_total_columns = 0;
  for (auto w : track_widths) score_total_columns += w;

  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_score_scroll_row != current_score_scroll_row
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
    current_score_scroll_row = new_score_scroll_row;
    
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
  // auto & pattern = song.getPattern(synth.getPatternPosition());
  size_t num_columns = song.getTracks().size();

  if (input.hasCtrl()) {
    if (input.getId() == 'a' || input.getId() == 'A') {
      new_score_cursor_col = 0;
      return true;
    } else if (input.getId() == 'e' || input.getId() == 'E') {
      new_score_cursor_col = num_columns > 1 ? num_columns - 1 : 0;
      return true;
    } else if (input.getId() == 't' || input.getId() == 'T') {
      int instrument_id = 0; // pattern.getTracks().back().getInstrumentId();
      auto & seq = song.addTrack();
      seq.setInstrumentId(instrument_id); // + 1);
      song.incVersion();
    } else if (input.hasShift() && (input.getId() == 't' || input.getId() == 'T')) {
      // delete track
      return true;
    } else if (input.getId() == 'g' || input.getId() == 'G') {
      // create group
      return true;
    } else if (input.getId() == 'd' || input.getId() == 'D') {
      // duplicate track
      return true;
    } else if (input.hasAlt() && input.getId() == NCKEY_LEFT) {
      // move selected track to left
      return true;
    } else if (input.hasAlt() && input.getId() == NCKEY_RIGHT) {
      // move selected track to right
      return true;
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
  } else {
    int midi_note = input.toMidiNote();
    if (midi_note != -1) {
      Note note(midi_note, 0x3f);
      auto & pattern = song.getPattern(synth.getPatternPosition());
      auto & track = song.getTrack(current_score_cursor_col);
      auto & instrument = song.getInstrument(track.getInstrumentId());
      auto & state = track.getState();
      state.playNote(note, instrument.getTranspose(), instrument.getDetune());

      if (input.hasShift()) {
	pattern.pushNote(current_score_cursor_col, synth.getTrackPosition(), note);
      } else {
	pattern.setNote(current_score_cursor_col, synth.getTrackPosition(), 0, note);
      }
      row_edited = true;

      if (!synth.isPlaying()) {
	if (input.getId() == NCKEY_BACKSPACE) synth.moveBackwards(song);
	else if (input.getId() != NCKEY_DEL) synth.moveForward(song);
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

  auto & tracks = song.getTracks();

  size_t current_pos = 6;
  for (size_t i = 0; i < tracks.size(); i++) {
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
    current_pos += actual_width;
  }
}

void
PatternEditor::renderRow(const StyleProvider & styles, const std::vector<size_t> & track_widths, size_t row, bool highlight) {
  auto [rows, cols] = getDim();

  if (row >= current_score_scroll_row && row < current_score_scroll_row + rows - 3) {
    auto & song = getController().getSong();
    auto & synth = getController().getSynth();
    auto & pattern = song.getPattern(synth.getPatternPosition());
    
    string padding;
    for (size_t i = 1; i < cols - 1; i++) padding += ' ';
    
    setBgColor(styles.window_bg_color);
    putstr(2 + row - current_score_scroll_row, 1, padding);
    
    auto & tracks = song.getTracks();
    
    size_t current_pos = 1;
    for (int i = -1; i < (int)tracks.size(); i++) {
      auto & track = tracks[i];
      
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
	
	putstr(2 + row - current_score_scroll_row, current_pos, format(" {:02x} ", row));
	
	setFgColor(styles.window_border_color);
	setBgColor(styles.window_bg_color);
	
	putstr(2 + row - current_score_scroll_row, current_pos + 4, "│");
	
	current_pos += 5;
      } else {
	auto notes = pattern.getNotes(i, row);
	auto note_columns = i < track_widths.size() ? track_widths[i] : 0;
	if (note_columns == 0) note_columns = 1;
      	
	for (size_t k = 0; k < note_columns; k++) {
	  string s;
	  if (k < notes.size() && notes[k].isDefined()) {
	    auto & note = notes[k];
	    s = note.toString() + format(" {:02x}", note.getVelocity());
	  } else {
	    s = "... ..";
	  }
	  setFgColor(cell_fg);
	  setBgColor(cell_bg);
	  
	  putstr(2 + row - current_score_scroll_row, current_pos, s);
	  
	  setFgColor(styles.window_border_color);
	  setBgColor(bg);
	  
	  putstr(2 + row - current_score_scroll_row, current_pos + 6, "│");
	  
	  current_pos += 7;
	}
      }
    }

    auto & annotation = pattern.getAnnotation(row);
    if (!annotation.empty()) {
      setFgColor("#e03030");
      setBgColor("#702020");
      putstr(2 + row - current_score_scroll_row, current_pos + 2, annotation);
    }
  }
}
