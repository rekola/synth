#include <cellui/Lamp.h>

namespace cellui {

void drawLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom) {
  if (row < clip_top || row >= clip_bottom) return;
  plane.setFgColor(plane.getStyles().lampColor(lit, background));
  plane.setBgColor(background);
  plane.putstr(row, col, kLampGlyph);
}

}  // namespace cellui
