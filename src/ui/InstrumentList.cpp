#include "InstrumentList.h"

#include "../Controller.h"
#include "../model/Song.h"
#include "../playback/InputEvent.h"
#include "StyleProvider.h"

#include <fmt/core.h>

using namespace std;

bool
InstrumentList::render(const StyleProvider & styles, bool refresh) {
  bool render_all = refresh;
  auto & song = getController().getSong();

  if (song.getMajorVersion() != current_song_version || new_scroll_pos != current_scroll_pos) {
    render_all = true;
  }

  bool need_refresh = false;
  if (render_all) {
    current_scroll_pos = new_scroll_pos;
    
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    
    getPlane().drawBorder();
    
    for (int i = 0; i < static_cast<int>(song.getInstruments().size()); i++) {
      renderRow(styles, i, i == new_cursor_row);
    }
    need_refresh = true;
  } else if (new_cursor_row != current_cursor_row) {
    renderRow(styles, current_cursor_row, false);
    renderRow(styles, new_cursor_row, true);
    need_refresh = true;
  }

  current_song_version = song.getMajorVersion();
  current_cursor_row = new_cursor_row;
  
  return need_refresh;
}

void
InstrumentList::renderRow(const StyleProvider & styles, int row, bool highlight) {
  auto [rows, cols] = getDim();

  if (row >= current_scroll_pos && row < current_scroll_pos + rows - 2) {
    auto & song = getController().getSong();
    auto & instrument = *(song.getInstruments()[static_cast<size_t>(row)]);
        
    if (highlight) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
    } else {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
    }
    auto line = instrument.getDisplayName();
    while (int(line.size()) < cols - 2) line += ' ';
    putstr(1 + row - current_scroll_pos, 1, line);
  }
}

bool
InstrumentList::offerInput(const InputEvent & input) {
  auto & song = getController().getSong();
  auto [rows, cols] = getDim();
  auto num_instruments = static_cast<int>(song.getInstruments().size());

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
#if 0
    int midi_note = input.toMidiNote();
    if (midi_note != -1) {
      Note note(midi_note, 0x40);
      auto & instrument = song.getInstrument(new_cursor_row);
    }
#endif
  }
  
  return false;
}
