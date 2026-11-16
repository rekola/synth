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

LinearContext nativeLinearContext(unsigned width, unsigned height, float display_scale, bool prefer_float) {
  LinearContext result;
  canvas::PixelSize size{ width, height };
#if defined(CANVAS_HAS_QUARTZ)
  thread_local std::shared_ptr<canvas::Quartz2DCache> cache = std::make_shared<canvas::Quartz2DCache>();
  if (prefer_float) {
    try {
      result.context = std::make_unique<canvas::ContextQuartz2D>(cache, size, 4, display_scale, canvas::PixelType::FLOAT32);
      result.is_float = true;
    } catch (const canvas::UnsupportedFormatException &) {
    }
  }
  if (!result.context) result.context = std::make_unique<canvas::ContextQuartz2D>(cache, size, 4, display_scale);
#elif defined(CANVAS_HAS_GDIPLUS)
  (void)prefer_float;  // GDI+ has no float surfaces
  result.context = std::make_unique<canvas::ContextGDIPlus>(size, 4, display_scale);
#elif defined(CANVAS_HAS_CAIRO)
  if (prefer_float) {
    try {
      result.context = std::make_unique<canvas::ContextCairo>(size, 4, display_scale, canvas::PixelType::FLOAT32);
      result.is_float = true;
    } catch (const canvas::UnsupportedFormatException &) {
    }
  }
  if (!result.context) result.context = std::make_unique<canvas::ContextCairo>(size, 4, display_scale);
#else
  (void)size;
  (void)display_scale;
  (void)prefer_float;
#endif
  if (result.context) result.context->setInputColorSpace(canvas::ColorSpace::LINEAR_REC2020);
  return result;
}

Picture readPicture(LinearContext & drawn, unsigned width, unsigned height, float display_scale) {
  Picture out;
  if (!drawn.context || width == 0 || height == 0) return out;
  auto image = drawn.context->getDefaultSurface().createImage(display_scale);
  const canvas::ImageData & data = image->getData();
  if (data.getNumChannels() != 4 || data.getWidth() < width || data.getHeight() < height) return out;
  bool bgr = data.getChannelOrder() == canvas::ChannelOrder::BGR;
  out.width = static_cast<int>(width);
  out.height = static_cast<int>(height);
  if (drawn.is_float) {
    if (data.getPixelType() != canvas::PixelType::FLOAT32) return Picture();
    out.hdr.resize(static_cast<size_t>(width) * height * 4);
    const float * src = data.getFloatData();
    const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
    for (unsigned y = 0; y < height; y++) {
      for (unsigned x = 0; x < width; x++) {
        const float * p = src + y * stride + static_cast<size_t>(x) * 4;
        float a = p[3], un = a > 0.0f && a < 1.0f ? 1.0f / a : 1.0f;  // read back premultiplied
        float * o = &out.hdr[(static_cast<size_t>(y) * width + x) * 4];
        o[0] = p[bgr ? 2 : 0] * un;
        o[1] = p[1] * un;
        o[2] = p[bgr ? 0 : 2] * un;
        o[3] = a;
      }
    }
    return out;
  }
  // The surface holds premultiplied B, G, R, A; everything drawn is opaque, so swapping the
  // first and third byte gives straight R, G, B, A.
  out.pixels.resize(static_cast<size_t>(width) * height);
  const unsigned char * src = data.getData();
  const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
  for (unsigned y = 0; y < height; y++) {
    for (unsigned x = 0; x < width; x++) {
      const unsigned char * p = src + y * stride + static_cast<size_t>(x) * 4;
      uint32_t r = p[bgr ? 2 : 0], g = p[1], b = p[bgr ? 0 : 2];
      out.pixels[static_cast<size_t>(y) * width + x] = r | (g << 8) | (b << 16) | (0xffu << 24);
    }
  }
  return out;
}

}  // namespace cellui
