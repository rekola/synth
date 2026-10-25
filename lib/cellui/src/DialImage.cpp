#include <cellui/Dial.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>

namespace cellui {

namespace dial_image {

namespace {

constexpr double kPi = 3.14159265358979323846;
// The ring is open this far either side of straight down, where the letter sits.
constexpr double kGapHalfAngle = 35.0 * kPi / 180.0;
constexpr float kRingPixels = 4.0f;
constexpr float kOutlinePixels = 2.0f;

canvas::Color toCanvas(const Color & c) {
  auto rgb = c.toSrgb8();
  return canvas::Color(static_cast<float>(rgb[0]) / 255.0f, static_cast<float>(rgb[1]) / 255.0f, static_cast<float>(rgb[2]) / 255.0f, 1.0f);
}

}  // namespace

std::vector<uint32_t> render(const DialReading & reading, const DialLook & look, unsigned width, unsigned height, float scale) {
  std::vector<uint32_t> out;
  if (width == 0 || height == 0) return out;
  auto context = cellui::nativeContext(width, height);
  if (!context) return out;
  canvas::Context & ctx = *context;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  ctx.fillStyle = toCanvas(look.background);
  ctx.fillRect(0, 0, w, h);

  const float ring = kRingPixels * std::max(1.0f, std::round(scale));
  const float outline = kOutlinePixels * std::max(1.0f, std::round(scale));
  const float cx = w / 2.0f, cy = h / 2.0f;
  // The dial fills the smaller side less a pixel of margin.
  const float outer = std::max(ring, std::min(w, h) / 2.0f - 3.0f * std::max(1.0f, std::round(scale)));
  const float radius = outer - ring / 2.0f;  // the ring's centre line

  // The ring: canvas angles run clockwise from the right, so straight down is pi/2.
  // While typing, the dial is dimmed under what is typed.
  if (reading.editing) ctx.globalAlpha = 0.3f;
  const double start = kPi / 2.0 + kGapHalfAngle;
  const double sweep = 2.0 * kPi - 2.0 * kGapHalfAngle;
  ctx.strokeStyle = toCanvas(look.foreground);
  ctx.lineWidth = ring;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, start, start + sweep);
  ctx.stroke();

  // The pointer, in a notch of background cut out of the ring so that it stays
  // apart from it: the outline, then the line itself, flush with the ring's outer edge.
  const double angle = start + sweep * std::clamp(reading.fraction, 0.0f, 1.0f);
  const float dx = static_cast<float>(std::cos(angle)), dy = static_cast<float>(std::sin(angle));
  ctx.strokeStyle = toCanvas(look.background);
  ctx.lineWidth = ring + 2.0f * outline;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + dx * (outer + outline), cy + dy * (outer + outline));
  ctx.stroke();
  ctx.strokeStyle = toCanvas(look.foreground);
  ctx.lineWidth = ring;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + dx * outer, cy + dy * outer);
  ctx.stroke();

  // The letter, in the gap at the bottom.
  if (!reading.letter.empty()) {
    ctx.fillStyle = toCanvas(look.foreground);
    ctx.font.family = "sans-serif";
    ctx.font.weight = "bold";
    ctx.font.size = std::max(6.0f, radius * 0.6f);
    ctx.textBaseline = canvas::TextBaseline::MIDDLE;
    ctx.textAlign = canvas::TextAlign::CENTER;
    ctx.fillText(reading.letter, cx, cy + radius);
  }

  // What is being typed, in a translucent box over the middle of the dial,
  // with a caret.
  if (reading.editing) {
    ctx.globalAlpha = 1.0f;
    const float box_w = w * 0.9f, box_h = std::max(h * 0.34f, 12.0f);
    ctx.fillStyle = toCanvas(look.background);
    ctx.globalAlpha = 0.8f;
    ctx.fillRect(cx - box_w / 2.0f, cy - box_h / 2.0f, box_w, box_h);
    ctx.globalAlpha = 1.0f;
    ctx.fillStyle = toCanvas(look.foreground);
    ctx.font.family = "sans-serif";
    ctx.font.weight = "bold";
    ctx.font.size = std::max(8.0f, box_h * 0.7f);
    ctx.textBaseline = canvas::TextBaseline::MIDDLE;
    ctx.textAlign = canvas::TextAlign::LEFT;
    // Left-aligned so the caret follows the text; the tail if it is too wide.
    std::string text = reading.edit_text;
    float text_w = static_cast<float>(ctx.measureText(text).width);
    while (text_w > box_w - 8.0f && !text.empty()) {
      text.erase(0, 1);
      text_w = static_cast<float>(ctx.measureText(text).width);
    }
    const float x0 = cx - box_w / 2.0f + 4.0f;
    ctx.fillText(text, x0, cy);
    ctx.fillRect(x0 + text_w + 1.0f, cy - box_h * 0.35f, std::max(1.0f, std::round(scale)), box_h * 0.7f);
  }

  // The surface holds premultiplied B, G, R, A; everything is opaque, so
  // swapping the first and third byte gives straight R, G, B, A.
  auto image = ctx.getDefaultSurface().createImage(1.0f);
  const canvas::ImageData & data = image->getData();
  out.assign(static_cast<size_t>(width) * height, 0);
  if (data.getNumChannels() != 4 || data.getWidth() < width || data.getHeight() < height) return out;
  const unsigned char * src = data.getData();
  const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
  for (unsigned y = 0; y < height; y++) {
    for (unsigned x = 0; x < width; x++) {
      const unsigned char * p = src + y * stride + static_cast<size_t>(x) * 4;
      uint32_t b = p[0], g = p[1], r = p[2];
      out[static_cast<size_t>(y) * width + x] = r | (g << 8) | (b << 16) | (0xffu << 24);
    }
  }
  return out;
}

}  // namespace dial_image

}  // namespace cellui
