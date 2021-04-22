#include "InstrumentList.h"

#include "Controller.h"
#include "Song.h"

#include <fmt/core.h>

using namespace std;

bool
InstrumentList::render(bool refresh) {
  bool render_all = refresh;
  auto & song = getController().getSong();

  if (song.getVersion() != current_song_version) {
    render_all = true;
  }
  
  if (render_all) {
    getPlane().drawBorder();
    
    for (size_t i = 0; i < song.getInstruments().size(); i++) {
      renderRow(i, *(song.getInstruments()[i]), false);
    }
  } else {
    
  }

  current_song_version = song.getVersion();
  
  return true;
}

void
InstrumentList::renderRow(size_t row, const Instrument & instrument, bool highlight) {
  auto & name = instrument.getName();
  putstr(1 + row, 1, name);
}
