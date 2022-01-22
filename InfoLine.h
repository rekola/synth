#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "Controller.h"
#include "SongState.h"

#include <fmt/core.h>
#include <iostream>

using namespace std;

class InfoLine : public UIElement {
 public:
  InfoLine(UIPlane & parent) : UIElement(parent) {
    setBgColor(120, 120, 120);
    setFgColor(30, 30, 30);
  }

  bool render(const StyleProvider & styles, bool refresh = false) {
    auto & info = getController().getPlaybackInfo();
    auto & song = getController().getSong();

    int new_version = song.getVersion();
    size_t new_position = info.getAbsolutePosition();
    
    if (refresh || new_version != current_version || new_position != current_position) {
      int seconds = (int)info.getTime();
      int minutes = seconds / 60;
      seconds %= 60;
      
      auto [ rows, cols ] = getDim();
      
      size_t pattern_idx = info.getPatternIndex();
      size_t num_voices = info.getVoiceCount();
      size_t num_allocated_voices = info.getAllocatedVoiceCount();
      
      auto s = fmt::format(" {:02x} {:02d}:{:02d} pattern:{} voices:{}/{}", info.getAbsolutePosition(), minutes, seconds, pattern_idx, num_voices, num_allocated_voices);
      if (info.isPlaying()) s += " PLAYING";
      while (s.size() < cols) s += ' ';
      
      putstr(0, 0, s);

      auto & pattern = song.getPattern(info.getPatternIndex());
      Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();

      // setFgColor(styles.window_border_color);
      // setBgColor(styles.window_bg_color);

      std::string tuning_text = to_string(tuning);

      int new_key = pattern.getKey() >= 0 ? pattern.getKey() : song.getKey();
      std::string key = new_key >= 0 ? Note::keyToString(tuning, new_key) : "?";
      int tempo = song.getTempo();

      int edit_step_size = 0, current_score_cursor_track = 0, current_score_cursor_col = 0;
      putstr(0, cols / 2, fmt::format("{:2d} {} {} {} {}:{}", edit_step_size, tuning_text, key, tempo, current_score_cursor_track, current_score_cursor_col));

      current_version = new_version;
      current_position = new_position;

      return true;
    } else {
      return false;
    }
  }

private:
  size_t current_position = 0;
  int current_version = 0;
};

#endif
