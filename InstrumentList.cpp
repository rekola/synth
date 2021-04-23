#include "InstrumentList.h"

#include "Controller.h"
#include "Song.h"
#include "UIInput.h"

#include <fmt/core.h>

using namespace std;

bool
InstrumentList::render(bool refresh) {
  bool render_all = refresh;
  auto & song = getController().getSong();

  auto [rows, cols] = getDim();

  if (song.getVersion() != current_song_version || new_scroll_pos != current_scroll_pos) {
    render_all = true;
  }

  bool need_refresh = false;
  if (render_all) {
    getPlane().drawBorder();
    
    for (size_t i = 0; i < song.getInstruments().size(); i++) {
      renderRow(new_scroll_pos, i, i == new_cursor_row);
    }
    need_refresh = true;
  } else if (new_cursor_row != current_cursor_row) {
    renderRow(current_scroll_pos, current_cursor_row, false);
    renderRow(current_scroll_pos, new_cursor_row, true);
    need_refresh = true;
  }

  current_song_version = song.getVersion();
  current_cursor_row = new_cursor_row;
  current_scroll_pos = new_scroll_pos;
  
  return need_refresh;
}

void
InstrumentList::renderRow(size_t scroll_pos, size_t row, bool highlight) {
  auto [rows, cols] = getDim();

  if (row >= scroll_pos && row < scroll_pos + rows - 2) {
    auto & song = getController().getSong();
    auto & instrument = *(song.getInstruments()[row]);
        
    if (highlight) {
      setFgColor(0x00, 0x00, 0x00);
      setBgColor(0xa0, 0xff, 0xa0);
    } else {
      setFgColor(0xe0, 0xe0, 0xe0);
      setBgColor(0x20, 0x20, 0x20);
    }
    auto line = instrument.getName();
    while (line.size() < cols - 2) line += ' ';
    putstr(1 + row - scroll_pos, 1, line);
  }
}

bool
InstrumentList::offerInput(const UIInput & input) {
  auto & song = getController().getSong();
  auto [rows, cols] = getDim();
  size_t num_instruments = song.getInstruments().size();

  if (input.getId() == NCKEY_UP) {
    if (new_cursor_row > 0) new_cursor_row--;
    if (new_cursor_row < new_scroll_pos) new_scroll_pos = new_cursor_row;
    return true;
  } else if (input.getId() == NCKEY_DOWN) {
    if (new_cursor_row + 1 < num_instruments) new_cursor_row++;
    if (new_cursor_row >= new_scroll_pos + rows - 2) new_scroll_pos = new_cursor_row - (rows - 2) + 1;
    return true;
  } else if (input.getId() == NCKEY_ENTER) {
    // select instrument
    return true;
  } else {
    int midi_note = input.toMidiNote();
    if (midi_note != -1) {
      Note note(midi_note);
      song.getInstrument(new_cursor_row).playNote(note);
    }
  }
  
  return false;
}
