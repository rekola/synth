#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"
#include "Tuning.h"
#include "Cursor.h"

#include <vector>
#include <unordered_map>

class Synth;
class InputEvent;
class StyleProvider;
class Song;
class VisibleTrackInfo;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const InputEvent & input) override;
  void handleMidiEvent(MidiEvent & ev) override;

protected:
  std::unordered_map<int, VisibleTrackInfo> getTrackInformation(const Song & song) const;
  void renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info);
  void renderRow(const StyleProvider & styles, size_t heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, VisibleTrackInfo> & track_info, size_t row, bool highlight);

  Cursor current_cursor, new_cursor;
  
  size_t current_score_playing_row = 0;
  size_t current_score_pattern = 0;  
  size_t current_score_total_columns = 0;
  size_t current_scroll_row = 0, current_scroll_track = 0, current_scroll_col = 0;
  int current_tempo = 0;
  int current_keyboard_octave = 4;
  
  int edit_step_size = 1, new_edit_step_size = 1;
  bool row_edited = false;
  int current_song_version = 0;

  std::vector<int> active_midi_notes;
};

#endif
