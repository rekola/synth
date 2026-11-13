// Times the picture encoder on pictures the library draws itself, and prints a hash of the bytes it
// makes, so a change that is meant to leave the bytes alone can be shown to. Usage: cellui_sixel_bench [rounds].
#include <cellui/BufferPlane.h>
#include <cellui/HeatmapImage.h>
#include <cellui/Image.h>
#include <cellui/LineImage.h>
#include <cellui/StageStats.h>
#include <cellui/StyleProvider.h>
#include <cellui/term/ImageEncoder.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>


using namespace cellui;
using namespace cellui::term;

namespace {

Image fromPixels(std::vector<uint32_t> pixels, int width, int height) {
  Image img;
  img.width = width;
  img.height = height;
  img.pixels = std::move(pixels);
  return img;
}

uint64_t fnv(const std::string & s, uint64_t h = 1469598103934665603ull) {
  for (char c : s) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
  return h;
}

struct Picture {
  const char * name;
  Image image;
};

}  // namespace

int main(int argc, char ** argv) {
  int rounds = argc > 1 ? std::atoi(argv[1]) : 20;
  StageStats::enable();
  StyleProvider styles(Theme::dark());
  {
    // What drawing the pictures costs, a call each.
    std::vector<float> levels(328, 0.5f);
    const int cols = 24, rows = 12;
    std::vector<float> bright(static_cast<size_t>(cols * rows), 0.5f), sat(static_cast<size_t>(cols * rows), 0.5f);
    auto time_us = [&](auto && draw) {
      auto t0 = std::chrono::steady_clock::now();
      for (int r = 0; r < rounds; r++) draw();
      return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / rounds;
    };
    std::printf("draw: spectrum 328x80 %.0f us, heatmap 232x80 %.0f us\n", time_us([&] { line_image::render(levels, 328, 80, styles); }),
                time_us([&] { heatmap_image::render(bright, sat, cols, rows, {}, 232, 80, styles); }));
  }
  std::vector<Picture> pictures;
  {
    std::vector<float> levels(328);
    for (size_t i = 0; i < levels.size(); i++) levels[i] = 0.35f + 0.3f * std::sin(static_cast<float>(i) * 0.07f) + 0.1f * std::sin(static_cast<float>(i) * 0.4f);
    pictures.push_back({"spectrum 328x80", fromPixels(line_image::render(levels, 328, 80, styles), 328, 80)});
  }
  {
    const int cols = 24, rows = 12;
    std::vector<float> bright(static_cast<size_t>(cols * rows)), sat(static_cast<size_t>(cols * rows));
    for (size_t i = 0; i < bright.size(); i++) {
      bright[i] = 0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.37f);
      sat[i] = 0.3f + 0.7f * std::fabs(std::sin(static_cast<float>(i) * 0.11f));
    }
    pictures.push_back({"heatmap 232x80", fromPixels(heatmap_image::render(bright, sat, cols, rows, {}, 232, 80, styles), 232, 80)});
  }
  {
    // A blurred backdrop: smooth, dark, bluish, stretched over its cells.
    Image blur;
    blur.width = 64;
    blur.height = 24;
    blur.pixels.resize(64 * 24);
    for (int y = 0; y < 24; y++)
      for (int x = 0; x < 64; x++)
        blur.pixels[static_cast<size_t>(y * 64 + x)] = Image::pack(static_cast<uint8_t>(16 + x / 6 + y / 8), static_cast<uint8_t>(22 + x / 5 + y / 6), static_cast<uint8_t>(26 + x / 4));
    blur.cols = 32;
    blur.rows = 4;
    pictures.push_back({"gradient 64x24 stretched", std::move(blur)});
  }
  {
    // A flat picture with a few colours and hard edges: text-like.
    Image flat;
    flat.width = 120;
    flat.height = 48;
    flat.pixels.assign(120 * 48, Image::pack(28, 34, 39));
    for (int y = 8; y < 40; y++)
      for (int x = 8 + (y / 4) * 3; x < 40 + (y / 4) * 3; x++) flat.pixels[static_cast<size_t>(y * 120 + x)] = Image::pack(117, 212, 235);
    pictures.push_back({"flat 120x48", std::move(flat)});
  }

  struct Mode {
    const char * name;
    PixelProtocol protocol;
    PictureQuality quality;
  };
  const Mode modes[] = {
      {"sixel full", PixelProtocol::SIXEL, PictureQuality::FULL},
      {"sixel reduced", PixelProtocol::SIXEL, PictureQuality::REDUCED},
      {"kitty full", PixelProtocol::KITTY, PictureQuality::FULL},
      {"kitty reduced", PixelProtocol::KITTY, PictureQuality::REDUCED},
      {"iterm2 full", PixelProtocol::ITERM2, PictureQuality::FULL},
  };
  uint64_t all = 0;
  auto stages_before = StageStats::totals();
  std::printf("%-26s %-14s %9s %10s  %s\n", "picture", "mode", "bytes", "us/encode", "hash");
  for (const Mode & mode : modes) {
    for (const Picture & pic : pictures) {
      ImageEncoder::Profile prof;
      prof.protocol = mode.protocol;
      prof.cell_px_height = 6;
      prof.cell_px_width = 2;
      uint64_t hash = 0;
      size_t bytes = 0;
      auto start = std::chrono::steady_clock::now();
      for (int r = 0; r < rounds; r++) {
        ImageEncoder enc(prof);
        enc.setPictureQuality(mode.quality);
        int cols = pic.image.stretched() ? pic.image.cols : (pic.image.width + 1) / 2, rows = pic.image.stretched() ? pic.image.rows : (pic.image.height + 5) / 6;
        CellBuffer b(rows + 1, cols);
        CellBuffer::Cell marked;
        marked.image = 1;
        for (int y = 0; y < rows; y++)
          for (int x = 0; x < cols; x++) b.put(y, x, marked);
        ImagePlacement place;
        place.image = std::make_shared<const Image>(pic.image);
        place.key = 1;
        std::string out = enc.encode(b, {place}, true);
        if (r == 0) {
          hash = fnv(out);
          bytes = out.size();
        }
      }
      double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / rounds;
      auto stages_now = StageStats::totals();
      std::string stage_text;
      for (size_t i = 0; i < kStageCount; i++) {
        int64_t d = (stages_now[i] - stages_before[i]) / 1000 / rounds;
        if (d > 0) stage_text += std::string(" ") + stageName(static_cast<Stage>(i)) + "=" + std::to_string(d);
      }
      stages_before = stages_now;
      all = fnv(std::to_string(hash), all);
      std::printf("%-26s %-14s %9zu %10.0f  %016llx%s\n", pic.name, mode.name, bytes, us, static_cast<unsigned long long>(hash), stage_text.c_str());
    }
  }
  std::printf("all: %016llx\n", static_cast<unsigned long long>(all));
  return 0;
}
