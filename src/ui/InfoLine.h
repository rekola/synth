#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "../Controller.h"
#include "../state/SongState.h"
#include "../util/Utf8.h"

#include <algorithm>
#include <fmt/format.h>

using namespace std;

class InfoLine : public UIElement {
 public:
  InfoLine(Plane & parent) : UIElement(parent) {
    setBgColor(getPlane().getStyles().info_line_bg_color);
    setFgColor(getPlane().getStyles().info_line_fg_color);
  }

  // What the link to the display is, when it is not at full speed ("" for nothing to show).
  void setLinkText(std::string text) { link_text_ = std::move(text); }
  // Columns at the right end taken by another widget (the octave selector).
  void setRightReserved(int cols) { right_reserved_ = cols; }
  // The column where the transport button goes: right after the buffer name.
  int buttonColumn() const { return button_col_; }

  bool render(const StyleProvider & styles, bool refresh = false) {
    auto & info = getController().getPlaybackInfo();
    auto & song = getController().getSong();
    auto buffer_name = getController().getActiveBufferName();
    // The disambiguated display text getBufferDisplayName() computes for
    // *any* open buffer can change when some *other* buffer is added/
    // killed/renamed - e.g. opening a second "song.xml" elsewhere means
    // the active one now needs its own "<dir>" suffix too, even though
    // its own name/version/position/voice counts never changed. Buffer
    // names is its own dirty-check input for exactly this - see
    // current_buffer_names_'s own comment below.
    auto buffer_names = getController().getBufferNames();

    auto new_version = song.getMajorVersion();
    auto new_position = info.getAbsolutePosition();
    auto num_voices = info.getVoiceCount();
    auto num_allocated_voices = info.getAllocatedVoiceCount();
    auto latency_ms = info.getRoundTripLatencyMs();
    auto is_playing = info.isPlaying();
    // The tempo and time signature in force at the position: the playing
    // tempo while the transport runs, else the arrangement's at the row.
    auto tempo_text = fmt::format("♩{} {}", is_playing ? info.getTempo() : song.getArrangementTempoAt(new_position), song.getBarsAt(new_position).signature.toString());
    auto modified = getController().hasUnsavedChanges();
    // A file system question: asked again only when the buffer or its saved state changes.
    if (buffer_name != read_only_name_ || modified != current_modified_) {
      read_only_ = getController().isActiveBufferFileReadOnly();
      read_only_name_ = buffer_name;
    }

    if (refresh || new_version != current_version_ || new_position != current_position_ ||
        num_voices != current_num_voices_ || num_allocated_voices != current_num_allocated_voices_ ||
        buffer_name != current_buffer_name_ || buffer_names != current_buffer_names_ ||
        latency_ms != current_latency_ms_ || right_reserved_ != current_right_reserved_ || is_playing != current_is_playing_ || tempo_text != current_tempo_text_ || modified != current_modified_ || link_text_ != current_link_text_) {
      auto [ rows, cols ] = getDim();

      // Controller::getBufferDisplayName()'s own text - basename-only,
      // unless another open buffer shares it, in which case just enough
      // of the parent directory is appended to tell them apart (Emacs-
      // style uniquify - see that method's own comment) - the same
      // display convention the Buffers menu uses (TerminalMenu::rebuild()),
      // so the same buffer reads the same way in both places.
      auto buffer_display_name = getController().getBufferDisplayName(buffer_name);
      // Live input's round-trip latency, only while capture runs (nothing
      // to show otherwise, and no space taken for it) - "~" for a nominal
      // figure, one that couldn't be measured.
      auto latency = latency_ms < 0 ? std::string() :
        fmt::format(" lat {}{}ms", info.isRoundTripLatencyNominal() ? "~" : "", latency_ms);
      // As the Emacs mode line: the modified/read-only pair ("**" modified, "%%" read-only,
      // "%*" both, "--" neither) and the remote marker ("-" for a local file), then the name.
      auto flags = read_only_ ? (modified ? "%*-" : "%%-") : (modified ? "**-" : "---");
      auto prefix = fmt::format("{} {} ", flags, buffer_display_name);
      // The transport button (TransportButton) is laid over the blank cells after the name.
      button_col_ = static_cast<int>(Utf8::displayWidth(prefix));
      // The transport's musical position (Song::formatPosition()).
      auto s = prefix + std::string(4, ' ') + fmt::format("{} {}{} voices:{}/{}", song.formatPosition(info.getAbsolutePosition()), tempo_text, latency, num_voices, num_allocated_voices);
      while (s.size() < static_cast<size_t>(cols)) s += ' ';

      putstr(0, 0, s);

      // setFgColor(styles.window_border_color);
      // setBgColor(styles.window_bg_color);

      // The link, when it is not at full speed: right-aligned beside the octave selector.
      if (!link_text_.empty()) {
        auto w = Utf8::displayWidth(link_text_);
        putstr(0, std::max(0, cols - right_reserved_ - w - 1), link_text_);
      }

      current_version_ = new_version;
      current_position_ = new_position;
      current_num_voices_ = num_voices;
      current_num_allocated_voices_ = num_allocated_voices;
      current_latency_ms_ = latency_ms;
      current_is_playing_ = is_playing;
      current_tempo_text_ = std::move(tempo_text);
      current_modified_ = modified;
      current_link_text_ = link_text_;
      current_right_reserved_ = right_reserved_;
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
  int current_latency_ms_ = -1;
  // Stopping at the same row changes nothing else shown.
  bool current_is_playing_ = false, current_modified_ = false;
  std::string current_tempo_text_;
  int button_col_ = 1;
  bool read_only_ = false;
  std::string read_only_name_;
  std::string link_text_, current_link_text_;
  int right_reserved_ = 0, current_right_reserved_ = 0;
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
