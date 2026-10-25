#include "NativeContext.h"

#if defined(CANVAS_HAS_QUARTZ)
#include <canvas/ContextQuartz2D.h>
#elif defined(CANVAS_HAS_GDIPLUS)
#include <canvas/ContextGDIPlus.h>
#elif defined(CANVAS_HAS_CAIRO)
#include <canvas/ContextCairo.h>
#endif

namespace cellui {

std::unique_ptr<canvas::Context> nativeContext(unsigned width, unsigned height, unsigned channels, float display_scale) {
#if defined(CANVAS_HAS_QUARTZ)
  // Fonts and colour spaces are kept in a cache the contexts share; it is not
  // thread safe, so each thread has its own.
  thread_local std::shared_ptr<canvas::Quartz2DCache> cache = std::make_shared<canvas::Quartz2DCache>();
  return std::make_unique<canvas::ContextQuartz2D>(cache, width, height, channels, display_scale);
#elif defined(CANVAS_HAS_GDIPLUS)
  return std::make_unique<canvas::ContextGDIPlus>(width, height, channels, display_scale);
#elif defined(CANVAS_HAS_CAIRO)
  return std::make_unique<canvas::ContextCairo>(width, height, channels, display_scale);
#else
  (void)width;
  (void)height;
  (void)channels;
  (void)display_scale;
  return nullptr;
#endif
}

}  // namespace cellui
