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

  if (song.getVersion() != current_song_version) {
    render_all = true;
  }

  bool need_refresh = false;
  if (render_all) {
    getPlane().drawBorder();
    
    for (size_t i = 0; i < song.getInstruments().size() && i < rows - 2; i++) {
      renderRow(i, i == current_cursor_row);
    }
    need_refresh = true;
  } else if (new_cursor_row != current_cursor_row) {
    renderRow(current_cursor_row, false);
    renderRow(new_cursor_row, true);
    need_refresh = true;
  }

  current_song_version = song.getVersion();
  current_cursor_row = new_cursor_row;
  
  return need_refresh;
}

void
InstrumentList::renderRow(size_t row, bool highlight) {
  auto & song = getController().getSong();
  auto & instrument = *(song.getInstruments()[row]);
  auto [rows, cols] = getDim();

  if (highlight) {
    setFgColor(0x00, 0x00, 0x00);
    setBgColor(0xa0, 0xff, 0xa0);
  } else {
    setFgColor(0xe0, 0xe0, 0xe0);
    setBgColor(0x20, 0x20, 0x20);
  }
  auto line = instrument.getName();
  while (line.size() < cols - 2) line += ' ';
  putstr(1 + row, 1, line);
}

bool
InstrumentList::offerInput(const UIInput & input) {
  auto & song = getController().getSong();
  size_t num_rows = song.getInstruments().size();

  if (input.getId() == NCKEY_UP) {
    if (new_cursor_row > 0) new_cursor_row--;
    return true;
  } else if (input.getId() == NCKEY_DOWN) {
    if (new_cursor_row + 1 < num_rows) new_cursor_row++;
    return true;
  } else if (input.getId() == NCKEY_ENTER) {
    // select instrument
    return true;
  }
  
  return false;
}
