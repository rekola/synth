#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "../Controller.h"
#include "../state/SongState.h"

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
    auto & buffer_name = getController().getActiveBufferName();
    // The disambiguated display text getBufferDisplayName() computes for
    // *any* open buffer can change when some *other* buffer is added/
    // killed/renamed - e.g. opening a second "song.xml" elsewhere means
    // the active one now needs its own "<dir>" suffix too, even though
    // its own name/version/position/voice counts never changed. Buffer
    // names is its own dirty-check input for exactly this - see
    // current_buffer_names_'s own comment below.
    auto buffer_names = getController().getBufferNames();

    auto new_version = song.getVersion();
    auto new_position = info.getAbsolutePosition();
    auto num_voices = info.getVoiceCount();
    auto num_allocated_voices = info.getAllocatedVoiceCount();

    if (refresh || new_version != current_version_ || new_position != current_position_ ||
	num_voices != current_num_voices_ || num_allocated_voices != current_num_allocated_voices_ ||
	buffer_name != current_buffer_name_ || buffer_names != current_buffer_names_) {
      auto seconds = (int)info.getTime();
      auto minutes = seconds / 60;
      seconds %= 60;

      auto [ rows, cols ] = getDim();

      auto pattern_idx = info.getPatternIndex();

      // Controller::getBufferDisplayName()'s own text - basename-only,
      // unless another open buffer shares it, in which case just enough
      // of the parent directory is appended to tell them apart (Emacs-
      // style uniquify - see that method's own comment) - the same
      // display convention the Buffers menu uses (TerminalMenu::rebuild()),
      // so the same buffer reads the same way in both places.
      auto buffer_display_name = getController().getBufferDisplayName(buffer_name);
      auto s = fmt::format(" {} {:02x} {:02d}:{:02d} pattern:{} voices:{}/{}", buffer_display_name, info.getAbsolutePosition(), minutes, seconds, pattern_idx, num_voices, num_allocated_voices);
      if (info.isPlaying()) s += " PLAYING";
      while (s.size() < static_cast<size_t>(cols)) s += ' ';

      putstr(0, 0, s);

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
      current_buffer_name_ = buffer_name;
      current_buffer_names_ = std::move(buffer_names);

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
  // Its own dirty-check input too, for the same reason: switching to a
  // different open buffer (Controller::switchToBuffer()) doesn't
  // necessarily change the new song's own version number to something
  // different from whatever the old one happened to be at, so relying on
  // new_version alone could leave the printed buffer name stale.
  std::string current_buffer_name_;
  // Every open buffer's name, not just the active one - the active
  // buffer's own *displayed* text (getBufferDisplayName()) depends on
  // whether some other open buffer collides with it, so this needs its
  // own dirty-check input distinct from current_buffer_name_ above: an
  // unrelated buffer opening/closing/renaming elsewhere can change it
  // without the active buffer's own name, version, position, or voice
  // counts changing at all.
  std::vector<std::string> current_buffer_names_;
};

#endif
