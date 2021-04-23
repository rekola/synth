#include "PatternEditor.h"

#include "UIInput.h"
#include "Synth.h"
#include "Controller.h"

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
  size_t score_section = synth.getSectionPosition();
  size_t score_playing_row = synth.getTrackPosition();
  auto & song = getController().getSong();
  auto & current_section = song.getSection(score_section);
  
  if (score_section != current_score_section ||
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
    getPlane().drawBorder();
    renderHeading();
    for (size_t row = 0; row < (size_t)rows && row < current_section.getRowCount(); row++) {
      renderRow(row, row == score_playing_row);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderRow(current_score_playing_row, false);
    renderRow(score_playing_row, true);
    need_redraw = true;
  } else if (cursor_col_changed || row_edited) {
    renderRow(score_playing_row, true);
    need_redraw = true;
  }
  
  current_score_section = score_section;
  current_score_playing_row = score_playing_row;
  current_song_version = song.getVersion();
  row_edited = false;
  
  return need_redraw;
}

bool
PatternEditor::offerInput(const UIInput & input) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & section = song.getSection(synth.getSectionPosition());
  size_t num_columns = section.getTracks().size();

  if (input.hasCtrl()) {
    if (input.getId() == 'a' || input.getId() == 'A') {
      new_score_cursor_col = 0;
      return true;
    } else if (input.getId() == 'e' || input.getId() == 'E') {
      new_score_cursor_col = num_columns > 1 ? num_columns - 1 : 0;
      return true;
    } else if (input.getId() == 't' || input.getId() == 'T') {
      int instrument_id = section.getTracks().back().getInstrumentId();
      auto & seq = section.addTrack();
      seq.setInstrumentId(instrument_id + 1);
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
      auto & section = song.getSection(synth.getSectionPosition());
      auto & track = section.getTrack(current_score_cursor_col);
      song.getInstrument(track.getInstrumentId()).playNote(note);
      track.setNote(synth.getTrackPosition(), note);
      row_edited = true;
      if (input.getId() == NCKEY_BACKSPACE) synth.moveBackwards(song);
      else if (input.getId() != NCKEY_DEL) synth.moveForward(song);
      
      return true;
    }
  }
  
  return false;
}

void
PatternEditor::renderHeading() {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & section = song.getSection(synth.getSectionPosition());

  setBgColor(0x00, 0x00, 0x00);
    
  putstr(1, 1, "   ");
  
  for (int i = 0; i < (int)section.getTrackCount(); i++) {
    setFgColor(0x00, 0x00, 0x00);
    setBgColor(0xf0, 0x80, 0x10);
    
    string name = format("Trk {:02d}│", i);    
    putstr(1, 1 + 3 + i*7, name);    
  }
}

void
PatternEditor::renderRow(size_t row, bool highlight) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & section = song.getSection(synth.getSectionPosition());
      
  for (int i = -1; i < (int)section.getTrackCount(); i++) {
    if (highlight && i == (int)current_score_cursor_col) {
      setFgColor(0x00, 0x00, 0x00);
      setBgColor(0xa0, 0xff, 0xa0);
    } else if (highlight) {
      setFgColor(0x80, 0xc0, 0x80);
      setBgColor(0x80, 0xa0, 0x80);
    } else if (row % 4 == 0) {
      setFgColor(0xe0, 0xe0, 0xe0);
      setBgColor(0x20, 0x20, 0x20);
    } else {
      setFgColor(0xa0, 0xa0, 0xa0);
      setBgColor(0x00, 0x00, 0x00);
    }

    if (i == -1) {
      auto s = format("{:02x}│", row);
      putstr(2 + row, 1, s);

    } else {
      auto & track = section.getTrack(i);

      if (track.getType() == Track::NOTES) {
	auto & note = track.getNote(row);
	
	string s;
	if (note.isDefined()) {
	  s = note.toString() + " .. ";
	} else {
	  s = "... .. ";
	}
	putstr(2 + row, 1 + 3 + i*7, s);
      } else {
	
      }
    }
  }
}
