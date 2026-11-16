#include <cellui/HdrBloom.h>

#include <cellui/Color.h>
#include <cellui/HdrSplit.h>

#include <algorithm>
#include <cmath>

namespace cellui {

namespace {

constexpr float kInv255 = 1.0f / 255.0f;

// Three box blurs approximate a Gaussian of deviation `sigma`, along one axis of `n` lines of `len`
// samples with `stride` between samples of a line and `step` between lines. Edges read as transparent.
void blurAxis(std::vector<float> & data, std::vector<float> & tmp, int len, int lines, size_t stride, size_t step, float sigma) {
  if (sigma < 0.3f || len <= 0) return;
  // Box widths whose three passes have the variance of the Gaussian (the usual three-box recipe).
  const int passes = 3;
  float ideal = std::sqrt(12.0f * sigma * sigma / passes + 1.0f);
  int lower = static_cast<int>(std::floor(ideal));
  if (lower % 2 == 0) lower--;
  lower = std::max(lower, 1);
  int upper = lower + 2;
  int small = static_cast<int>(std::lround((12.0f * sigma * sigma - passes * lower * lower - 4.0f * passes * lower - 3.0f * passes) / (-4.0f * lower - 4.0f)));
  tmp.resize(data.size());
  for (int pass = 0; pass < passes; pass++) {
    int radius = ((pass < small ? lower : upper) - 1) / 2;
    if (radius <= 0) continue;
    float norm = 1.0f / static_cast<float>(2 * radius + 1);
    for (int line = 0; line < lines; line++) {
      const float * in = &data[static_cast<size_t>(line) * step];
      float * out = &tmp[static_cast<size_t>(line) * step];
      float sum = 0.0f;
      for (int i = 0; i <= std::min(radius, len - 1); i++) sum += in[static_cast<size_t>(i) * stride];
      for (int i = 0; i < len; i++) {
        out[static_cast<size_t>(i) * stride] = sum * norm;
        int add = i + radius + 1, drop = i - radius;
        if (add < len) sum += in[static_cast<size_t>(add) * stride];
        if (drop >= 0) sum -= in[static_cast<size_t>(drop) * stride];
      }
    }
    data.swap(tmp);
  }
}

// `rgba` is premultiplied, 4 floats a pixel.
void blur(std::vector<float> & rgba, int width, int height, float sigma) {
  std::vector<float> tmp;
  // Four channels are blurred as one line of 4-float pixels by walking each channel in turn.
  for (size_t channel = 0; channel < 4; channel++) {
    std::vector<float> plane(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < plane.size(); i++) plane[i] = rgba[i * 4 + channel];
    blurAxis(plane, tmp, width, height, 1, static_cast<size_t>(width), sigma);
    blurAxis(plane, tmp, height, width, static_cast<size_t>(width), 1, sigma);
    for (size_t i = 0; i < plane.size(); i++) rgba[i * 4 + channel] = plane[i];
  }
}

// The glow layer as premultiplied floats (the colour times its alpha).
std::vector<float> glowFloats(const std::vector<uint32_t> & glow) {
  std::vector<float> out(glow.size() * 4);
  for (size_t i = 0; i < glow.size(); i++) {
    float a = static_cast<float>(Image::alpha(glow[i])) * kInv255;
    out[i * 4 + 0] = static_cast<float>(Image::red(glow[i])) * kInv255 * a;
    out[i * 4 + 1] = static_cast<float>(Image::green(glow[i])) * kInv255 * a;
    out[i * 4 + 2] = static_cast<float>(Image::blue(glow[i])) * kInv255 * a;
    out[i * 4 + 3] = a;
  }
  return out;
}

// One blurred layer, screened over `base` (encoded sRGB floats, 3 a pixel): the alpha is multiplied by
// `gain` and has `threshold` taken off after the blur, the colour is left as it is, and the whole is scaled
// by `strength`.
void screenLayer(std::vector<float> & base, std::vector<float> glow, int width, int height, float sigma, float gain, float threshold,
                 float strength) {
  blur(glow, width, height, sigma);
  for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); i++) {
    float a = glow[i * 4 + 3];
    if (a <= 0.0f) continue;
    float alpha = std::clamp(a * gain - threshold, 0.0f, 1.0f) * strength;
    if (alpha <= 0.0f) continue;
    for (size_t c = 0; c < 3; c++) {
      float colour = std::clamp(glow[i * 4 + c] / a, 0.0f, 1.0f);
      float light = colour * alpha;
      float & b = base[i * 3 + c];
      b = 1.0f - (1.0f - b) * (1.0f - light);
    }
  }
}

}  // namespace

static Image flattenFull(const Image & image, int cell_h) {
  if (!image.isHdr()) return image;
  Image out = image;
  out.hdr.clear();
  out.hdr.shrink_to_fit();
  HdrLayers cut = splitHdr(image.hdr, image.width, image.height);
  out.pixels = std::move(cut.base);
  if (cut.glow.empty() || out.pixels.empty()) return out;

  const size_t count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  std::vector<float> base(count * 3);
  for (size_t i = 0; i < count; i++) {
    base[i * 3 + 0] = static_cast<float>(Image::red(out.pixels[i])) * kInv255;
    base[i * 3 + 1] = static_cast<float>(Image::green(out.pixels[i])) * kInv255;
    base[i * 3 + 2] = static_cast<float>(Image::blue(out.pixels[i])) * kInv255;
  }
  // The window's blur radius is twice its deviation.
  const float h = static_cast<float>(std::max(cell_h, 1));
  std::vector<float> glow = glowFloats(cut.glow);
  // The halo is under the core in the window's blend, but screening commutes: the order does not matter.
  screenLayer(base, glow, image.width, image.height, static_cast<float>(kPictureCoreRadius) * h / 2.0f, static_cast<float>(kPictureCoreGain),
              static_cast<float>(kPictureCoreThreshold), static_cast<float>(kPictureCoreStrength));
  screenLayer(base, std::move(glow), image.width, image.height, static_cast<float>(kPictureHaloRadius) * h / 2.0f,
              static_cast<float>(kPictureHaloGain), 0.0f, static_cast<float>(kPictureHaloStrength));
  for (size_t i = 0; i < count; i++) {
    auto q = [&](size_t c) { return static_cast<uint8_t>(std::lround(std::clamp(base[i * 3 + c], 0.0f, 1.0f) * 255.0f)); };
    out.pixels[i] = Image::pack(q(0), q(1), q(2), Image::alpha(out.pixels[i]));
  }
  return out;
}

int hdrBloomReach(int cell_h) {
  double sigma = kPictureHaloRadius * std::max(cell_h, 1) / 2.0;
  return static_cast<int>(std::ceil(3.5 * sigma)) + 4;
}

Image flattenHdr(const Image & image, int cell_h, HdrFlattenState * state) {
  if (!image.isHdr() || !state) return flattenFull(image, cell_h);
  const int w = image.width, h = image.height;
  HdrFlattenState & s = *state;
  const bool same = s.source.size() == image.hdr.size() && s.result.width == w && s.result.height == h && s.cell_h == cell_h &&
                    s.result.pixels.size() == static_cast<size_t>(w) * static_cast<size_t>(h);
  if (!same) {
    s.result = flattenFull(image, cell_h);
    s.source = image.hdr;
    s.cell_h = cell_h;
    return s.result;
  }
  // The rectangle of pixels whose floats differ.
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; y++) {
    const float * a = &image.hdr[static_cast<size_t>(y) * static_cast<size_t>(w) * 4];
    const float * b = &s.source[static_cast<size_t>(y) * static_cast<size_t>(w) * 4];
    if (std::equal(a, a + static_cast<size_t>(w) * 4, b)) continue;
    for (int x = 0; x < w; x++) {
      if (!std::equal(a + x * 4, a + x * 4 + 4, b + x * 4)) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
      }
    }
    y0 = std::min(y0, y);
    y1 = std::max(y1, y);
  }
  s.result.version = image.version;
  s.result.cols = image.cols;
  s.result.rows = image.rows;
  s.result.pad = image.pad;
  if (x1 < 0) return s.result;
  const int reach = hdrBloomReach(cell_h);
  // What changes is the damage grown by the reach; it is computed from a crop twice the reach around the
  // damage, so the crop's own edge (where the blur sees transparency) never touches a pixel kept.
  auto clampx = [&](int v) { return std::clamp(v, 0, w); };
  auto clampy = [&](int v) { return std::clamp(v, 0, h); };
  const int ux0 = clampx(x0 - reach), uy0 = clampy(y0 - reach), ux1 = clampx(x1 + 1 + reach), uy1 = clampy(y1 + 1 + reach);
  const int cx0 = clampx(x0 - 2 * reach), cy0 = clampy(y0 - 2 * reach), cx1 = clampx(x1 + 1 + 2 * reach), cy1 = clampy(y1 + 1 + 2 * reach);
  Image crop;
  crop.width = cx1 - cx0;
  crop.height = cy1 - cy0;
  crop.hdr.resize(static_cast<size_t>(crop.width) * static_cast<size_t>(crop.height) * 4);
  for (int y = cy0; y < cy1; y++) {
    const float * from = &image.hdr[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(cx0)) * 4];
    std::copy(from, from + static_cast<size_t>(crop.width) * 4, &crop.hdr[static_cast<size_t>(y - cy0) * static_cast<size_t>(crop.width) * 4]);
  }
  Image done = flattenFull(crop, cell_h);
  for (int y = uy0; y < uy1; y++) {
    const uint32_t * from = &done.pixels[static_cast<size_t>(y - cy0) * static_cast<size_t>(crop.width) + static_cast<size_t>(ux0 - cx0)];
    std::copy(from, from + (ux1 - ux0), &s.result.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(ux0)]);
  }
  s.source = image.hdr;
  return s.result;
}

}  // namespace cellui
