#ifndef CELLUI_NATIVECONTEXT_H
#define CELLUI_NATIVECONTEXT_H

#include <canvas/Context.h>

#include <memory>

namespace cellui {

// A drawing context on the platform's own canvas backend: Cairo, Quartz 2D on
// Apple platforms or GDI+ on Windows.
std::unique_ptr<canvas::Context> nativeContext(unsigned width, unsigned height, unsigned channels = 4, float display_scale = 1.0f);

}  // namespace cellui

#endif
