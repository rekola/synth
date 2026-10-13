#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/term/FrameEncoder.h>
#include <cellui/term/ImageEncoder.h>

using namespace cellui;
using namespace cellui::term;

namespace {

std::vector<uint32_t> solid(int w, int h, uint32_t p) { return std::vector<uint32_t>(static_cast<size_t>(w * h), p); }

ImageEncoder::Profile profile(PixelProtocol protocol) {
  ImageEncoder::Profile p;
  p.protocol = protocol;
  p.cell_px_height = 6;
  p.cell_px_width = 2;
  return p;
}

size_t count(const std::string & s, const std::string & part) {
  size_t n = 0;
  for (size_t i = s.find(part); i != std::string::npos; i = s.find(part, i + 1)) n++;
  return n;
}

}  // namespace

TEST(sixel_encoding) {
  Image img;
  img.width = 2;
  img.height = 6;
  img.pixels = {Image::pack(255, 0, 0), Image::pack(0, 0, 0, 0)};
  img.pixels.resize(12);
  for (int y = 0; y < 6; y++) {
    img.pixels[static_cast<size_t>(y * 2)] = Image::pack(255, 0, 0);
    img.pixels[static_cast<size_t>(y * 2 + 1)] = Image::pack(0, 0, 0, 0);
  }
  std::vector<bool> keep(12, true);
  CHECK(encodeSixel(img, 0, 0, 2, 6, keep) == "\x1bP0;1;0q\"1;1;2;6#180;2;100;0;0#180~-\x1b\\");
  // Runs longer than two are counted; colours share a band separated by $.
  Image wide;
  wide.width = 4;
  wide.height = 1;
  wide.pixels = {Image::pack(255, 255, 255), Image::pack(255, 255, 255), Image::pack(255, 255, 255), Image::pack(0, 0, 0)};
  std::vector<bool> all(4, true);
  CHECK(encodeSixel(wide, 0, 0, 4, 1, all) == "\x1bP0;1;0q\"1;1;4;1#0;2;0;0;0#215;2;100;100;100#0!3?@$#215!3@-\x1b\\");
}

TEST(images_compose_into_marked_cells) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(2, 3);
  chart->move(1, 1);
  int h = 0, w = 0;
  CHECK(chart->pixelSize(h, w) && h == 12 && w == 6);
  chart->drawImage(solid(6, 12, Image::pack(10, 20, 30)), 6, 12);
  auto popup = root.createChild();
  popup->resize(1, 2);
  popup->move(2, 3);
  popup->putstr(0, 0, "ab");
  root.refresh();
  CHECK(root.images().size() == 1);
  CHECK(b.at(1, 1).image == 1 && b.at(2, 2).image == 1);
  CHECK(b.at(2, 3).image == 0 && b.at(2, 3).text == "a");
  chart->erase();
  root.refresh();
  CHECK(root.images().empty() && b.at(1, 1).image == 0);
}

TEST(kitty_images_send_only_changes_and_crop_around_covers) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(2, 3);
  chart->drawImage(solid(6, 12, Image::pack(10, 20, 30)), 6, 12);
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  root.refresh();
  std::string first = enc.encode(b, root.images(), true);
  CHECK(count(first, "a=t,f=24,s=6,v=12,i=1,") == 1);
  CHECK(count(first, "a=p,i=1,p=1,x=0,y=0,w=6,h=12") == 1);
  root.refresh();
  CHECK(enc.encode(b, root.images(), false).empty());
  // A popup over the right column: the image is placed as what still shows.
  auto popup = root.createChild();
  popup->resize(2, 1);
  popup->move(0, 2);
  popup->putstr(0, 0, "x");
  popup->putstr(1, 0, "y");
  root.refresh();
  std::string covered = enc.encode(b, root.images(), false);
  CHECK(count(covered, "a=t") == 0);
  CHECK(count(covered, "a=p,i=1,p=1,x=0,y=0,w=4,h=12") == 1);
  popup.reset();
  // A new picture is a new id, placed before the old one is deleted.
  chart->drawImage(solid(6, 12, Image::pack(1, 2, 3)), 6, 12);
  root.refresh();
  std::string next = enc.encode(b, root.images(), false);
  CHECK(next.find("i=2,") < next.find("a=p,i=2") && next.find("a=p,i=2") < next.find("a=d,d=I,i=1,"));
  chart->erase();
  root.refresh();
  CHECK(enc.encode(b, root.images(), false) == "\x1b_Ga=d,d=I,i=2,q=2\x1b\\");
}

TEST(sixel_images_redraw_on_change_and_avoid_the_last_row) {
  StyleProvider styles;
  CellBuffer b(3, 4);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(3, 2);
  chart->drawImage(solid(4, 18, Image::pack(255, 0, 0)), 4, 18);
  ImageEncoder enc(profile(PixelProtocol::SIXEL));
  root.refresh();
  std::string s = enc.encode(b, root.images(), true);
  CHECK(s.compare(0, 6, "\x1b[1;1H") == 0);
  CHECK(s.find("\"1;1;4;12") != std::string::npos);  // two of the three rows
  root.refresh();
  CHECK(enc.encode(b, root.images(), false).empty());
  CHECK(!enc.encode(b, root.images(), true).empty());
}

TEST(frame_encoder_leaves_image_cells_to_the_image) {
  CellBuffer f(1, 4);
  EncoderProfile p;
  FrameEncoder e(p);
  f.write(0, 0, "abcd", {});
  e.encode(f);
  CellBuffer::Cell marked;
  marked.image = 1;
  f.put(0, 1, marked);
  f.put(0, 2, marked);
  CHECK(e.encode(f, {}, "IMG") == "IMG");
  f.write(0, 1, "  ", {});
  std::string back = e.encode(f);
  CHECK(back == "\x1b[1;2H  ");
}

namespace {

uint32_t be32(const std::vector<uint8_t> & d, size_t i) {
  return (uint32_t(d[i]) << 24) | (uint32_t(d[i + 1]) << 16) | (uint32_t(d[i + 2]) << 8) | d[i + 3];
}

// The scanlines of a PNG made by encodePng(): its stored deflate blocks
// joined, with the zlib checksum verified.
std::vector<uint8_t> pngScanlines(const std::vector<uint8_t> & png, uint32_t & width, uint32_t & height) {
  std::vector<uint8_t> raw;
  size_t i = 8;
  while (i + 8 <= png.size()) {
    uint32_t len = be32(png, i);
    std::string type(png.begin() + static_cast<long>(i + 4), png.begin() + static_cast<long>(i + 8));
    size_t data = i + 8;
    if (type == "IHDR") {
      width = be32(png, data);
      height = be32(png, data + 4);
    } else if (type == "IDAT") {
      size_t p = data + 2;
      bool last = false;
      while (!last) {
        last = png[p] & 1;
        size_t n = png[p + 1] | (png[p + 2] << 8);
        raw.insert(raw.end(), png.begin() + static_cast<long>(p + 5), png.begin() + static_cast<long>(p + 5 + n));
        p += 5 + n;
      }
      uint32_t a = 1, b = 0;
      for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
      }
      if (be32(png, p) != ((b << 16) | a)) raw.clear();
    }
    i = data + len + 4;
  }
  return raw;
}

}  // namespace

TEST(png_encoding_round_trips) {
  Image img;
  img.width = 3;
  img.height = 2;
  img.pixels = {Image::pack(1, 2, 3), Image::pack(4, 5, 6), Image::pack(7, 8, 9),
                Image::pack(10, 11, 12), Image::pack(13, 14, 15, 128), Image::pack(16, 17, 18)};
  std::vector<uint8_t> png = encodePng(img, 1, 0, 2, 2);
  CHECK(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> raw = pngScanlines(png, w, h);
  CHECK(w == 2 && h == 2);
  std::vector<uint8_t> want = {0, 4, 5, 6, 255, 7, 8, 9, 255, 0, 13, 14, 15, 128, 16, 17, 18, 255};
  CHECK(raw == want);
  // Larger than one stored block.
  Image big;
  big.width = 200;
  big.height = 100;
  big.pixels = solid(200, 100, Image::pack(9, 8, 7));
  raw = pngScanlines(encodePng(big, 0, 0, 200, 100), w, h);
  CHECK(raw.size() == 100u * (200u * 4 + 1));
}

TEST(iterm2_images_send_a_png_per_visible_rectangle) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(2, 3);
  chart->drawImage(solid(6, 12, Image::pack(10, 20, 30)), 6, 12);
  ImageEncoder enc(profile(PixelProtocol::ITERM2));
  root.refresh();
  std::string first = enc.encode(b, root.images(), true);
  CHECK(count(first, "\x1b]1337;File=inline=1;") == 1);
  CHECK(first.compare(0, 6, "\x1b[1;1H") == 0);
  CHECK(count(first, ";width=3;height=2;preserveAspectRatio=0;doNotMoveCursor=1:") == 1);
  root.refresh();
  CHECK(enc.encode(b, root.images(), false).empty());
  // A popup over the middle column of the first row leaves three rectangles.
  auto popup = root.createChild();
  popup->resize(1, 1);
  popup->move(0, 1);
  popup->putstr(0, 0, "x");
  root.refresh();
  std::string covered = enc.encode(b, root.images(), false);
  CHECK(count(covered, "\x1b]1337;File=") == 3);
  CHECK(count(covered, "\x1b[1;3H\x1b]1337;") == 1);
}
