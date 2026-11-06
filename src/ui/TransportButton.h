#ifndef _TRANSPORTBUTTON_H_
#define _TRANSPORTBUTTON_H_

#include "UIElement.h"
#include "../Controller.h"

// The transport state as one glyph on a raised, rounded plane of its own
// (a button to come): ▸ while playing, ▪ (small square) otherwise.
class TransportButton : public UIElement {
 public:
  static constexpr int kWidth = 3;

  explicit TransportButton(Plane & parent) : UIElement(parent) {
    auto & theme = getPlane().getStyles().theme;
    getPlane().setElevation(theme.elevation.raised);
    getPlane().setCornerRadius(theme.dialog_radius_px);
  }

  bool render(const StyleProvider & styles, bool refresh = false) {
    bool playing = getController().getPlaybackInfo().isPlaying();
    if (!refresh && drawn_ && playing == playing_) return false;
    setBgColor(styles.picker_bg_color);
    setFgColor(styles.picker_fg_color);
    putstr(0, 0, playing ? " ▸ " : " ▪ ");
    playing_ = playing;
    drawn_ = true;
    return true;
  }

 private:
  bool playing_ = false, drawn_ = false;
};

#endif
