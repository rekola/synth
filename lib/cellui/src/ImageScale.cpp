#include <cellui/ImageScale.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cellui {

Image scaleImage(const Image & src, int width, int height) {
  Image out;
  out.width = std::max(1, width);
  out.height = std::max(1, height);
  out.version = src.version;
  out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
  if (src.width <= 0 || src.height <= 0 || src.pixels.empty()) return out;
  // The two source columns and the weight of the second, for each column of the result: worked out once.
  struct Tap {
    int a, b;
    float t;
  };
  std::vector<Tap> columns(static_cast<size_t>(out.width));
  for (int x = 0; x < out.width; x++) {
    float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(src.width) / static_cast<float>(out.width) - 0.5f;
    int x0 = static_cast<int>(std::floor(fx));
    columns[static_cast<size_t>(x)] = Tap{std::clamp(x0, 0, src.width - 1), std::clamp(x0 + 1, 0, src.width - 1), fx - static_cast<float>(x0)};
  }
  for (int y = 0; y < out.height; y++) {
    float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(src.height) / static_cast<float>(out.height) - 0.5f;
    int y0 = static_cast<int>(std::floor(fy));
    float ty = fy - static_cast<float>(y0);
    const uint32_t * upper = &src.pixels[static_cast<size_t>(std::clamp(y0, 0, src.height - 1)) * static_cast<size_t>(src.width)];
    const uint32_t * lower = &src.pixels[static_cast<size_t>(std::clamp(y0 + 1, 0, src.height - 1)) * static_cast<size_t>(src.width)];
    uint32_t * row = &out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width)];
    for (int x = 0; x < out.width; x++) {
      const Tap & tap = columns[static_cast<size_t>(x)];
      uint32_t a = upper[tap.a], b = upper[tap.b], c = lower[tap.a], d = lower[tap.b];
      auto mix = [&](uint8_t (*channel)(uint32_t)) {
        float top = static_cast<float>(channel(a)) * (1 - tap.t) + static_cast<float>(channel(b)) * tap.t;
        float bottom = static_cast<float>(channel(c)) * (1 - tap.t) + static_cast<float>(channel(d)) * tap.t;
        return static_cast<uint8_t>(top * (1 - ty) + bottom * ty + 0.5f);
      };
      row[x] = Image::pack(mix(Image::red), mix(Image::green), mix(Image::blue), mix(Image::alpha));
    }
  }
  return out;
}

Image scaleImageNearest(const Image & src, int width, int height) {
  Image out;
  out.width = std::max(1, width);
  out.height = std::max(1, height);
  out.version = src.version;
  out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
  if (src.width <= 0 || src.height <= 0 || src.pixels.empty()) return out;
  std::vector<int> column(static_cast<size_t>(out.width));
  for (int x = 0; x < out.width; x++) column[static_cast<size_t>(x)] = std::min(src.width - 1, static_cast<int>(static_cast<int64_t>(x) * src.width / out.width));
  for (int y = 0; y < out.height; y++) {
    const uint32_t * row = &src.pixels[static_cast<size_t>(std::min(src.height - 1, static_cast<int>(static_cast<int64_t>(y) * src.height / out.height))) * static_cast<size_t>(src.width)];
    uint32_t * dest = &out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width)];
    for (int x = 0; x < out.width; x++) dest[x] = row[column[static_cast<size_t>(x)]];
  }
  return out;
}

}  // namespace cellui
