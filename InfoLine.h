#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "Controller.h"
#include "SongState.h"

#include <fmt/core.h>

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

    auto new_version = song.getVersion();
    auto new_position = info.getAbsolutePosition();
    auto num_voices = info.getVoiceCount();
    auto num_allocated_voices = info.getAllocatedVoiceCount();

    if (refresh || new_version != current_version_ || new_position != current_position_ ||
	num_voices != current_num_voices_ || num_allocated_voices != current_num_allocated_voices_) {
      auto seconds = (int)info.getTime();
      auto minutes = seconds / 60;
      seconds %= 60;

      auto [ rows, cols ] = getDim();

      auto pattern_idx = info.getPatternIndex();

      auto s = fmt::format(" {:02x} {:02d}:{:02d} pattern:{} voices:{}/{}", info.getAbsolutePosition(), minutes, seconds, pattern_idx, num_voices, num_allocated_voices);
      if (info.isPlaying()) s += " PLAYING";
      while (s.size() < cols) s += ' ';
      
      putstr(0, 0, s);

      auto & scene = song.getScene(info.getPatternIndex());

      // setFgColor(styles.window_border_color);
      // setBgColor(styles.window_bg_color);

      auto tuning_text = to_string(song.getTuning());
      auto key = song.getKey() >= 0 ? Note::keyToString(song.getTuning(), song.getKey()) : "?";
      auto tempo = song.getTempo();

      int edit_step_size = 0, current_score_cursor_track = 0, current_score_cursor_col = 0;
      putstr(0, cols / 2, fmt::format("{:2d} {} {} {} {}:{}", edit_step_size, tuning_text, key, tempo, current_score_cursor_track, current_score_cursor_col));

      current_version_ = new_version;
      current_position_ = new_position;
      current_num_voices_ = num_voices;
      current_num_allocated_voices_ = num_allocated_voices;

      return true;
    } else {
      return false;
    }
  }

private:
  int current_position_ = 0, current_version_ = 0;
  // Voice counts are their own dirty-check inputs (not just a byproduct
  // of a version/position change) - without this, a voice finishing its
  // release tail while the transport is stopped and nothing else is
  // being edited never bumps song version or playhead position, so the
  // printed count would otherwise freeze at whatever it was and never
  // tick down as voices actually finish.
  int current_num_voices_ = 0, current_num_allocated_voices_ = 0;
};

#endif
