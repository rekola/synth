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
  
  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version
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
    auto [rows, cols] = getDim();
    erase();
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    fill();
    getPlane().drawBorder();
    
    renderHeading(styles);
    for (size_t row = 0; row < (size_t)rows && row < current_pattern.getNumRows(); row++) {
      renderRow(styles, row, row == score_playing_row);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderRow(styles, current_score_playing_row, false);
    renderRow(styles, score_playing_row, true);
    need_redraw = true;
  } else if (cursor_col_changed || row_edited) {
    renderRow(styles, score_playing_row, true);
    need_redraw = true;
  }
  
  current_score_pattern = score_pattern;
  current_score_playing_row = score_playing_row;
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
  } else if (input.getId() == NCKEY_UP) {
    if (!synth.isPlaying()) synth.moveBackwards(song);
    return true;
  } else if (input.getId() == NCKEY_DOWN) {
    if (!synth.isPlaying()) synth.moveForward(song);
    return true;
  } else {
    int midi_note = input.toMidiNote();
    if (midi_note != -1) {
      Note note(midi_note);
      auto & pattern = song.getPattern(synth.getPatternPosition());
      auto & track = song.getTrack(current_score_cursor_col);
      auto & instrument = song.getInstrument(track.getInstrumentId());
      auto & state = track.getState();
      state.playNote(note, instrument.getTranspose(), instrument.getDetune());
      pattern.setNote(current_score_cursor_col, synth.getTrackPosition(), note);      
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
PatternEditor::renderHeading(const StyleProvider & styles) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & pattern = song.getPattern(synth.getPatternPosition());

  auto [rows, cols] = getDim();
  
  string padding;
  for (size_t i = 1; i < cols - 1; i++) padding += ' ';

  setBgColor(styles.window_bg_color);
  putstr(1, 1, padding);

  auto & tracks = song.getTracks();
  
  for (int i = 0; i < (int)tracks.size(); i++) {
    setFgColor(0x00, 0x00, 0x00);
    setBgColor(0xf0, 0x80, 0x10);
    
    string name = format("Trk {:02d}│", i);
    putstr(1, 1 + 4 + i*7, name);    
  }
}

void
PatternEditor::renderRow(const StyleProvider & styles, size_t row, bool highlight) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & pattern = song.getPattern(synth.getPatternPosition());

  auto [rows, cols] = getDim();
  
  string padding;
  for (size_t i = 1; i < cols - 1; i++) padding += ' ';

  setBgColor(styles.window_bg_color);
  putstr(2 + row, 1, padding);

  auto & tracks = song.getTracks();
  
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
    
    setFgColor(cell_fg);
    setBgColor(cell_bg);
    
    if (i == -1) {
      auto s = format(" {:02x}", row);
      putstr(2 + row, 1, s);

      setFgColor(styles.window_border_color);
      setBgColor(styles.window_bg_color);
      
      putstr(2 + row, 4, "│");
    } else {
      if (track.getType() == Track::NOTES) {
	auto & note = pattern.getNote(i, row);
	
	string s;
	if (note.isDefined()) {
	  s = note.toString() + " ..";
	} else {
	  s = "... ..";
	}
	putstr(2 + row, 1 + 4 + i*7, s);

	setFgColor(styles.window_border_color);
	setBgColor(bg);
	
	putstr(2 + row, 1 + 4 + i*7 + 6, "│");
      } else {
	
      }
    }
  }
}
