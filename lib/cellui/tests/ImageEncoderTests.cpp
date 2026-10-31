#include "TestFramework.h"
#include "Inflate.h"

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
  CHECK(enc.encode(b, root.images(), false) == "\x1b_Ga=d,d=I,i=2,q=1\x1b\\");
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
  // Blanked as the image arrives, so nothing under it can be selected; then
  // left to the image.
  CHECK(e.encode(f, {}, "IMG") == "\x1b[2G  IMG");
  CHECK(e.encode(f, {}, "").empty());
  f.write(0, 1, "  ", {});
  std::string back = e.encode(f);
  CHECK(back == "\x1b[1;2H  ");
}

namespace {

uint32_t be32(const std::vector<uint8_t> & d, size_t i) {
  return (uint32_t(d[i]) << 24) | (uint32_t(d[i + 1]) << 16) | (uint32_t(d[i + 2]) << 8) | d[i + 3];
}

// The scanlines of a PNG made by encodePng(), inflated (the check value verified).
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
      auto z = inflate_test::Inflater(png.data() + data, len).zlib();
      raw = z ? *z : std::vector<uint8_t>{};
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

TEST(kitty_a_scrolled_image_is_placed_again_and_not_sent) {
  StyleProvider styles;
  CellBuffer b(6, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  root.drawImageAt(1, 1, solid(2, 6, Image::pack(10, 20, 30)), 2, 6);
  root.refresh();
  CHECK(count(enc.encode(b, root.images(), true), "a=t") == 1);
  for (int row = 2; row <= 4; row++) {
    root.erase();
    root.drawImageAt(row, 1, solid(2, 6, Image::pack(10, 20, 30)), 2, 6);
    root.refresh();
    std::string moved = enc.encode(b, root.images(), false);
    CHECK(count(moved, "a=t") == 0 && count(moved, "a=d,d=I") == 0 && count(moved, "a=p,i=1,") == 1);
  }
}

// A picture smaller than the cells it covers is stretched over them by the
// terminal (kitty, iTerm2), or here before it is sent (Sixel).
TEST(stretched_images_are_scaled_over_their_cells) {
  CellBuffer b(3, 6);
  auto image = std::make_shared<Image>();
  image->width = 4;
  image->height = 6;
  image->pixels = solid(4, 6, Image::pack(200, 100, 50));
  image->cols = 4;
  image->rows = 2;
  image->version = 7;
  ImagePlacement p;
  p.image = image;
  p.key = 1;
  CellBuffer::Cell marked;
  marked.image = 1;
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 4; c++) b.put(r, c, marked);

  ImageEncoder kitty(profile(PixelProtocol::KITTY));
  std::string k = kitty.encode(b, {p}, true);
  CHECK(count(k, "a=t,f=24,s=4,v=6,") == 1);
  CHECK(count(k, "a=p,i=1,p=1,x=0,y=0,w=4,h=6,c=4,r=2,") == 1);

  // Half of it covered: half of its pixels, over half of its cells.
  for (int r = 0; r < 2; r++)
    for (int c = 2; c < 4; c++) b.put(r, c, CellBuffer::Cell{});
  std::string half = kitty.encode(b, {p}, false);
  CHECK(count(half, "x=0,y=0,w=2,h=6,c=2,r=2,") == 1);

  ImageEncoder iterm(profile(PixelProtocol::ITERM2));
  CHECK(count(iterm.encode(b, {p}, true), ";width=2;height=2;") == 1);

  ImageEncoder six(profile(PixelProtocol::SIXEL));
  CHECK(count(six.encode(b, {p}, true), "\x1bP") == 1);
}

// A padded picture is placed as its inside, the pad there for the terminal
// to blend the edges with as it scales.
TEST(padded_stretched_images_place_their_inside) {
  CellBuffer b(3, 4);  // iTerm2's pictures stay off the last row
  auto image = std::make_shared<Image>();
  image->width = 8;
  image->height = 10;
  image->pixels = solid(8, 10, Image::pack(1, 2, 3));
  image->cols = 4;
  image->rows = 2;
  image->pad = 2;
  image->version = 1;
  ImagePlacement p;
  p.image = image;
  p.key = 1;
  CellBuffer::Cell marked;
  marked.image = 1;
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 4; c++) b.put(r, c, marked);
  ImageEncoder kitty(profile(PixelProtocol::KITTY));
  CHECK(count(kitty.encode(b, {p}, true), "x=2,y=2,w=4,h=6,c=4,r=2,") == 1);
  ImageEncoder iterm(profile(PixelProtocol::ITERM2));
  CHECK(count(iterm.encode(b, {p}, true), ";width=4;height=2;") == 1);
}

// A colour between palette entries is dithered: a mix of entries that
// averages to it, not the nearest one everywhere.
TEST(sixel_images_are_dithered) {
  Dither fs;
  fs.method = Dither::Method::FLOYD_STEINBERG;
  Image img;
  img.width = 12;
  img.height = 12;
  img.pixels.assign(144, Image::pack(20, 20, 20));  // between the greys 18 and 28
  std::vector<bool> keep(144, true);
  for (const Dither & d : {Dither{}, fs}) {
    std::string s = encodeSixel(img, 0, 0, 12, 12, keep, d);
    CHECK(count(s, ";2;7;7;7") == 1);   // grey 18
    CHECK(count(s, ";2;11;11;11") == 1);  // grey 28
  }
}

// The ordered pattern belongs to the screen, not the image: two tiles that
// meet line up, and a redraw keeps it.
TEST(ordered_dithering_follows_the_screen) {
  Image img;
  img.width = 16;
  img.height = 12;
  img.pixels.assign(16 * 12, Image::pack(60, 70, 90));  // between cube levels
  std::vector<bool> keep(16 * 12, true);
  Dither at;
  at.origin_x = 3;
  at.origin_y = 5;
  Dither period = at;  // a whole matrix (8 pixels) on
  period.origin_x += 8;
  period.origin_y += 16;
  Dither shifted = at;
  shifted.origin_x += 1;
  std::string a = encodeSixel(img, 0, 0, 16, 12, keep, at);
  CHECK(a == encodeSixel(img, 0, 0, 16, 12, keep, period));
  CHECK(a != encodeSixel(img, 0, 0, 16, 12, keep, shifted));
  Dither nearest;
  nearest.method = Dither::Method::NEAREST;
  CHECK(count(encodeSixel(img, 0, 0, 16, 12, keep, nearest), ";2;") == 1);  // one colour
  CHECK(count(a, ";2;") > 1);
}

TEST(dither_methods_by_name) {
  Dither::Method m = Dither::Method::NEAREST;
  CHECK(Dither::byName("floyd-steinberg", m) && m == Dither::Method::FLOYD_STEINBERG);
  CHECK(Dither::byName("ordered", m) && m == Dither::Method::ORDERED);
  CHECK(Dither::byName("nearest", m) && m == Dither::Method::NEAREST);
  CHECK(!Dither::byName("bayer", m));
}

namespace {

std::vector<uint8_t> unbase64(const std::string & text) {
  std::vector<uint8_t> out;
  uint32_t acc = 0;
  int bits = 0;
  for (char c : text) {
    int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
    if (v < 0) continue;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>(acc >> bits));
    }
  }
  return out;
}

// The payload of a kitty upload that fits one chunk.
std::string kittyPayload(const std::string & s) {
  size_t start = s.find(';');
  size_t end = s.find("\x1b\\", start);
  return s.substr(start + 1, end - start - 1);
}

}  // namespace

TEST(kitty_uploads_are_deflated_and_a_refusal_turns_it_off) {
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
  CHECK(count(first, "o=z,a=t,f=24,s=6,v=12,i=1,") == 1);
  auto z = inflate_test::Inflater(unbase64(kittyPayload(first)).data(), unbase64(kittyPayload(first)).size()).zlib();
  CHECK(z && z->size() == 6u * 12 * 3 && (*z)[0] == 10 && (*z)[1] == 20 && (*z)[2] == 30);
  // The terminal refuses it: pictures go up raw from then on, the refused one again.
  CHECK(!enc.handleKittyReply("i=1;EINVAL:could not decompress").empty());
  CHECK(!enc.compressing());
  root.refresh();
  std::string again = enc.encode(b, root.images(), false);
  CHECK(count(again, "a=t,f=24,s=6,v=12,i=2,") == 1 && count(again, "o=z") == 0);
}
