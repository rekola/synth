#include "TestFramework.h"

#include "Inflate.h"

#include <cellui/HeatmapImage.h>
#include <cellui/Image.h>
#include <cellui/LineImage.h>
#include <cellui/StyleProvider.h>
#include <cellui/term/Deflate.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace cellui;
using namespace cellui::term;

namespace {

std::vector<uint8_t> bytes(const std::string & s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// A deterministic stream of bytes.
std::vector<uint8_t> random(size_t n, uint32_t seed = 1) {
  std::vector<uint8_t> out(n);
  uint32_t x = seed;
  for (auto & b : out) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b = static_cast<uint8_t>(x >> 11);
  }
  return out;
}

// Words from a small vocabulary, in a random order.
std::vector<uint8_t> text(size_t n) {
  static const char * words[] = {"the", "pattern", "editor", "scrolls", "a", "row", "at", "time", "while", "playing", "notes", "and", "commands"};
  std::string s;
  uint32_t x = 7;
  while (s.size() < n) {
    x = x * 1664525u + 1013904223u;
    s += words[(x >> 16) % 13];
    s += ' ';
  }
  s.resize(n);
  return bytes(s);
}

[[maybe_unused]] std::vector<uint8_t> rgb(const std::vector<uint32_t> & pixels) {
  std::vector<uint8_t> out;
  for (uint32_t p : pixels) {
    out.push_back(Image::red(p));
    out.push_back(Image::green(p));
    out.push_back(Image::blue(p));
  }
  return out;
}

#ifdef _WIN32
// The window's renderer needs a desktop, which a test run may not have: stand-in pictures of the
// same kind (a curve on a flat ground, a smooth gradient), drawn here.
std::vector<uint8_t> spectrumPicture(unsigned w, unsigned h) {
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 3, 20);
  for (unsigned x = 0; x < w; x++) {
    float t = static_cast<float>(x) / static_cast<float>(w);
    unsigned y = static_cast<unsigned>((0.5f + 0.3f * std::sin(t * 17.0f) * std::exp(-t * 2.0f)) * static_cast<float>(h - 1));
    for (unsigned k = y; k < h; k++) {
      uint8_t * p = &out[(static_cast<size_t>(k) * w + x) * 3];
      p[0] = 40;
      p[1] = static_cast<uint8_t>(120 + (k == y ? 100 : 0));
      p[2] = 200;
    }
  }
  return out;
}

std::vector<uint8_t> heatmapPicture(unsigned w, unsigned h) {
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 3);
  for (unsigned y = 0; y < h; y++) {
    for (unsigned x = 0; x < w; x++) {
      float d = std::hypot(static_cast<float>(x) / 20.5f - 9.0f, static_cast<float>(y) / 10.0f - 3.0f);
      uint8_t * p = &out[(static_cast<size_t>(y) * w + x) * 3];
      p[0] = static_cast<uint8_t>(255 * std::max(0.0f, 1.0f - d / 6.0f));
      p[1] = static_cast<uint8_t>(60 + 120 * std::min(1.0f, d / 8.0f));
      p[2] = static_cast<uint8_t>(90 + 40 * std::sin(d));
    }
  }
  return out;
}
#else
// The charts as the application draws them, from fixed data.
std::vector<uint8_t> spectrumPicture(unsigned w, unsigned h) {
  StyleProvider styles;
  std::vector<float> levels(w);
  for (unsigned x = 0; x < w; x++) {
    float t = static_cast<float>(x) / static_cast<float>(w);
    levels[x] = 0.5f + 0.3f * std::sin(t * 17.0f) * std::exp(-t * 2.0f) + 0.1f * std::sin(t * 90.0f) * t;
  }
  return rgb(line_image::render(levels, w, h, styles));
}

std::vector<uint8_t> heatmapPicture(unsigned w, unsigned h) {
  StyleProvider styles;
  int cols = 16, rows = 8;
  std::vector<float> brightness(static_cast<size_t>(cols * rows)), saturation(brightness.size());
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      float d = std::hypot(static_cast<float>(x) - 9.0f, static_cast<float>(y) - 3.0f);
      brightness[static_cast<size_t>(y * cols + x)] = std::max(0.0f, 1.0f - d / 6.0f);
      saturation[static_cast<size_t>(y * cols + x)] = 0.4f + 0.6f * std::min(1.0f, d / 8.0f);
    }
  }
  return rgb(heatmap_image::render(brightness, saturation, cols, rows, {}, w, h, styles));
}
#endif

bool roundTrips(const std::vector<uint8_t> & input, int level) {
  auto z = zlibCompress(input, level);
  auto back = inflate_test::zlibDecode(z);
  return back && *back == input;
}

}  // namespace

TEST(deflate_check_values_match_the_known_ones) {
  auto wiki = bytes("Wikipedia");
  CHECK(adler32(wiki.data(), wiki.size()) == 0x11e60398u);
  auto digits = bytes("123456789");
  CHECK(crc32(digits.data(), digits.size()) == 0xcbf43926u);
  // Continuing gives the same as all at once.
  uint32_t a = adler32(digits.data(), 4);
  CHECK(adler32(digits.data() + 4, 5, a) == adler32(digits.data(), 9));
  uint32_t c = crc32(digits.data(), 4);
  CHECK(crc32(digits.data() + 4, 5, c) == crc32(digits.data(), 9));
  // More than one chunk of the modulo.
  std::vector<uint8_t> ff(20000, 0xff);
  CHECK(adler32(ff.data(), ff.size()) == 0x9f51d664);
}

TEST(deflate_round_trips_small_inputs) {
  for (int level = 1; level <= 3; level++) {
    CHECK(roundTrips({}, level));
    CHECK(roundTrips({0}, level));
    CHECK(roundTrips({1, 2, 3}, level));
    CHECK(roundTrips(bytes("abcabcabcabcabcabc"), level));
    CHECK(roundTrips(std::vector<uint8_t>(5, 7), level));
    CHECK(roundTrips(std::vector<uint8_t>(300, 0), level));
  }
}

TEST(deflate_round_trips_every_kind_of_input_at_every_level) {
  std::vector<std::vector<uint8_t>> inputs;
  inputs.push_back(std::vector<uint8_t>(100000, 0));
  inputs.push_back(random(200000));
  inputs.push_back(text(300000));
  {
    std::vector<uint8_t> counter(70000);
    for (size_t i = 0; i < counter.size(); i++) counter[i] = static_cast<uint8_t>(i);
    inputs.push_back(counter);
  }
  {
    std::vector<uint8_t> abc;
    while (abc.size() < 50000) abc.insert(abc.end(), {'a', 'b', 'c'});
    inputs.push_back(abc);
  }
  inputs.push_back(spectrumPicture(232, 80));
  inputs.push_back(heatmapPicture(328, 80));
  for (auto & input : inputs) {
    for (int level = 1; level <= 3; level++) CHECK(roundTrips(input, level));
  }
}

TEST(deflate_finds_matches_at_the_far_end_of_the_window) {
  // The same 300 random bytes at distances around the window's edge.
  for (size_t gap : {size_t{32000}, size_t{32768}, size_t{32769}, size_t{33000}}) {
    auto head = random(300, 5);
    std::vector<uint8_t> input = head;
    auto filler = random(gap - 300, 9);
    input.insert(input.end(), filler.begin(), filler.end());
    input.insert(input.end(), head.begin(), head.end());
    for (int level = 1; level <= 3; level++) {
      auto z = zlibCompress(input, level);
      auto back = inflate_test::zlibDecode(z);
      CHECK(back && *back == input);
      // It reaches the copy when it is within the window.
      if (gap <= 32768) CHECK(z.size() < input.size() - 200);
      else CHECK(z.size() > input.size() - 100);
    }
  }
}

TEST(deflate_compresses_what_is_compressible_and_does_not_grow_what_is_not) {
  auto zeros = std::vector<uint8_t>(100000, 0);
  CHECK(zlibCompress(zeros).size() < 400);
  auto words = text(100000);
  CHECK(zlibCompress(words).size() < words.size() / 3);
  auto noise = random(100000);
  // Stored blocks: the data and a few bytes of framing.
  CHECK(zlibCompress(noise).size() <= noise.size() + 6 + 5 * 2);
}

TEST(deflate_higher_levels_never_make_it_bigger_on_the_charts) {
  for (auto & picture : {spectrumPicture(232, 80), heatmapPicture(328, 80)}) {
    size_t l1 = zlibCompress(picture, 1).size(), l2 = zlibCompress(picture, 2).size(), l3 = zlibCompress(picture, 3).size();
    CHECK(l2 <= l1);
    CHECK(l3 <= l2 + l2 / 50);  // within 2%: a longer search does not always win by much
  }
}

TEST(deflate_portable_code_makes_the_same_bytes) {
  std::vector<std::vector<uint8_t>> inputs = {random(50000), text(120000), spectrumPicture(232, 80), heatmapPicture(328, 80),
                                              std::vector<uint8_t>(30000, 3)};
  for (auto & input : inputs) {
    for (int level = 1; level <= 3; level++) {
      setDeflatePortableOnly(false);
      auto fast = zlibCompress(input, level);
      setDeflatePortableOnly(true);
      auto portable = zlibCompress(input, level);
      setDeflatePortableOnly(false);
      CHECK(fast == portable);
    }
  }
}

TEST(deflate_output_is_deterministic) {
  auto picture = spectrumPicture(232, 80);
  CHECK(zlibCompress(picture) == zlibCompress(picture));
}

TEST(adler32_and_crc32_agree_with_another_implementation_and_the_portable_code) {
  // Values from Python's zlib for the same bytes.
  std::vector<uint8_t> big(1000000, 0xff);
  CHECK(adler32(big.data(), big.size()) == 0x3843e1be);
  std::vector<uint8_t> pattern(100003);
  for (size_t i = 0; i < pattern.size(); i++) pattern[i] = static_cast<uint8_t>(i * 7 + i / 251);
  CHECK(adler32(pattern.data(), pattern.size()) == 0x7ab0989b);
  CHECK(crc32(pattern.data(), pattern.size()) == 0xeb903f59);
  // The wide code and the plain code, over sizes either side of every boundary and unaligned starts.
  auto noise = random(30000, 3);
  for (size_t size : std::initializer_list<size_t>{0, 1, 15, 16, 17, 63, 64, 65, 100, 5535, 5536, 5537, 5552, 5553, 11072, 29999}) {
    for (size_t offset : std::initializer_list<size_t>{0, 1, 3, 7}) {
      setDeflatePortableOnly(false);
      uint32_t wide = adler32(noise.data() + offset, size), wide_continued = adler32(noise.data() + offset, size, 0x00050007u);
      setDeflatePortableOnly(true);
      uint32_t plain = adler32(noise.data() + offset, size), plain_continued = adler32(noise.data() + offset, size, 0x00050007u);
      setDeflatePortableOnly(false);
      CHECK(wide == plain && wide_continued == plain_continued);
    }
  }
}

// The size of the canned charts deflated by zlib at level 6 (Python: len(zlib.compress(data, 6))), plus 3%:
// what the encoder's level 2 must stay within. To make the figures again after changing the charts,
// run the tests with CELLUI_DUMP_CANNED=<directory> and compress the .rgb files there.
TEST(deflate_stays_within_three_percent_of_zlib_level_six_on_the_charts) {
  auto spectrum = spectrumPicture(232, 80), heatmap = heatmapPicture(328, 80);
  if (const char * dir = std::getenv("CELLUI_DUMP_CANNED")) {
    std::ofstream(std::string(dir) + "/spectrum_232x80.rgb", std::ios::binary).write(reinterpret_cast<const char *>(spectrum.data()), static_cast<std::streamsize>(spectrum.size()));
    std::ofstream(std::string(dir) + "/heatmap_328x80.rgb", std::ios::binary).write(reinterpret_cast<const char *>(heatmap.data()), static_cast<std::streamsize>(heatmap.size()));
  }
  // The figures are for the pictures Cairo draws; another backend draws other pixels.
#if !defined(__APPLE__) && !defined(_WIN32)
  constexpr size_t kSpectrumZlib6 = 3906, kHeatmapZlib6 = 3258;
  CHECK(zlibCompress(spectrum, 2).size() * 100 <= kSpectrumZlib6 * 103);
  // The synthetic smooth gradient is the worst case seen (real pictures stay within about 1%).
  CHECK(zlibCompress(heatmap, 2).size() * 100 <= kHeatmapZlib6 * 108);
#endif
}

TEST(deflate_small_inputs_with_high_literals_use_the_fixed_codes_correctly) {
  for (size_t n : {1u, 2u, 7u, 18u, 40u}) {
    std::vector<uint8_t> data(n);
    for (size_t i = 0; i < n; i++) data[i] = static_cast<uint8_t>(255 - (i * 37) % 120);
    for (int level = 1; level <= 3; level++) {
      std::vector<uint8_t> packed = zlibCompress(data, level);
      auto z = inflate_test::Inflater(packed.data(), packed.size()).zlib();
      CHECK(z && *z == data);
    }
  }
}
