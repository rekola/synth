#ifndef CELLUI_BLENDMODE_H
#define CELLUI_BLENDMODE_H

#include <cellui/Color.h>

namespace cellui {

// How a colour combines with what is below it (the usual compositing blend
// modes). DIFFERENCE with a light colour inverts what lies under it, as a cell
// drawn in reversed colours does.
enum class BlendMode { NORMAL, MULTIPLY, SCREEN, OVERLAY, DARKEN, LIGHTEN, DIFFERENCE, EXCLUSION };

// `top` laid over `below` with `mode`, `alpha` of the way, per channel on the
// encoded sRGB values (so a window and a terminal show the same colours).
Color blendColors(BlendMode mode, const Color & below, const Color & top, float alpha);

}  // namespace cellui

#endif
