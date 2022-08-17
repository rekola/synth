#include "HierarchyView.h"

#include "Controller.h"
#include "Song.h"
#include "InputEvent.h"
#include "StyleProvider.h"

#include <fmt/core.h>

using namespace std;

bool
HierarchyView::render(const StyleProvider & styles, bool refresh) {
  bool render_all = refresh;
  auto & song = getController().getSong();
  auto & instrument_provider = getController().getInstrumentProvider();
  
  data_.clear();
  data_.push_back( { 0, TrackType::MASTER, "Song" });
  data_.push_back( { 1, TrackType::UNKNOWN, "Instruments" });
  for (size_t i = 0; i < song.getInstruments().size(); i++) {
    auto & instrument = *(song.getInstruments()[i]);       
    data_.push_back( { 2, TrackType::INSTRUMENT, instrument.getName() } );
  }
  data_.push_back( { 1, TrackType::UNKNOWN, "Tracks" });
  for (size_t i = 0; i < song.getTracks().size(); i++) {
    auto & track = song.getTracks()[i];
    data_.push_back( { 2, track->getType(), "Track #" + to_string(i) });
  }
  data_.push_back( { 0, TrackType::UNKNOWN, "Library" });
  data_.push_back( { 1, TrackType::UNKNOWN, "Rhythms" });
  data_.push_back( { 1, TrackType::UNKNOWN, "Instruments" });
  for (auto & [ name, instrument ] : instrument_provider.getInstruments()) {
    data_.push_back( { 2, TrackType::INSTRUMENT, name });
  }

  if (song.getVersion() != current_song_version_ || new_scroll_pos_ != current_scroll_pos_) {
    render_all = true;
  }

  bool need_refresh = false;
  if (render_all) {
    current_scroll_pos_ = new_scroll_pos_;
    
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    
    getPlane().drawBorder();
    
    auto [rows, cols] = getDim();
    for (int i = 0; i < rows - 2; i++) {
      renderRow(styles, i, i == new_cursor_row_ - current_scroll_pos_);
    }
    need_refresh = true;
  } else if (new_cursor_row_ != current_cursor_row_) {
    renderRow(styles, current_cursor_row_ - current_scroll_pos_, false);
    renderRow(styles, new_cursor_row_ - current_scroll_pos_, true);
    need_refresh = true;
  }

  current_song_version_ = song.getVersion();
  current_cursor_row_ = new_cursor_row_;
  
  return need_refresh;
}

void
HierarchyView::renderRow(const StyleProvider & styles, int display_row, bool highlight) {
  auto [rows, cols] = getDim();

  if (display_row >= 0 && display_row < rows - 2) {
    if (highlight) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
    } else {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
    }
  
    string padding(cols - 2, ' ');
    putstr(1 + display_row, 1, padding);   

    auto data_row = static_cast<size_t>(display_row + current_scroll_pos_);
    if (data_row < data_.size()) {
      auto & data = data_[data_row];
        
      putstr(1 + display_row, 1 + data.level * 3, data.label);
    }
  }
}

bool
HierarchyView::offerInput(const InputEvent & input) {
  auto & song = getController().getSong();
  auto [rows, cols] = getDim();

  if (input.getId() == NCKEY_UP) {
    if (new_cursor_row_ > 0) new_cursor_row_--;
    if (new_cursor_row_ < new_scroll_pos_) new_scroll_pos_ = new_cursor_row_;
    return true;
  } else if (input.getId() == NCKEY_DOWN) {
    if (new_cursor_row_ + 1 < static_cast<int>(data_.size())) new_cursor_row_++;
    if (new_cursor_row_ >= new_scroll_pos_ + rows - 2) new_scroll_pos_ = new_cursor_row_ - (rows - 2) + 1;
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
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, note.getValue(), note.getVelocity()));
    }
#endif
  }
  
  return false;
}
