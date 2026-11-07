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
  CHECK(encodeSixel(img, 0, 0, 2, 6, keep) == "\x1bP0;1;0q\"1;1;2;6#0;2;100;0;0#0~-\x1b\\");
  // Runs longer than two are counted; colours share a band separated by $.
  Image wide;
  wide.width = 4;
  wide.height = 1;
  wide.pixels = {Image::pack(255, 255, 255), Image::pack(255, 255, 255), Image::pack(255, 255, 255), Image::pack(0, 0, 0)};
  std::vector<bool> all(4, true);
  CHECK(encodeSixel(wide, 0, 0, 4, 1, all) == "\x1bP0;1;0q\"1;1;4;1#0;2;0;0;0#1;2;100;100;100#0!3?@$#1!3@-\x1b\\");
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

// Only the picture changed: the cells that differ are sent, the ones next to each other in a row as one.
TEST(sixel_images_that_change_send_only_the_cells_that_differ) {
  StyleProvider styles;
  CellBuffer b(5, 6);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(4, 6);
  std::vector<uint32_t> pixels = solid(12, 24, Image::pack(255, 0, 0));
  chart->drawImage(pixels, 12, 24);
  ImageEncoder enc(profile(PixelProtocol::SIXEL));
  root.refresh();
  CHECK(count(enc.encode(b, root.images(), true), "\x1bP") == 1);
  // The same pixels again: nothing.
  chart->drawImage(pixels, 12, 24);
  root.refresh();
  CHECK(enc.encode(b, root.images(), false).empty());
  // One pixel of the cell in row 1, column 2 (2 x 6 pixels a cell).
  pixels[static_cast<size_t>(8 * 12 + 4)] = Image::pack(0, 0, 255);
  chart->drawImage(pixels, 12, 24);
  root.refresh();
  std::string one = enc.encode(b, root.images(), false);
  CHECK(count(one, "\x1bP") == 1 && one.compare(0, 6, "\x1b[2;3H") == 0 && one.find("\"1;1;2;6") != std::string::npos);
  // Two neighbours in a row: one piece, four pixels wide.
  pixels[static_cast<size_t>(8 * 12 + 0)] = Image::pack(0, 255, 0);
  pixels[static_cast<size_t>(8 * 12 + 2)] = Image::pack(0, 255, 0);
  chart->drawImage(pixels, 12, 24);
  root.refresh();
  std::string two = enc.encode(b, root.images(), false);
  CHECK(count(two, "\x1bP") == 1 && two.find("\"1;1;4;6") != std::string::npos);
  // Most of it: the whole picture in one piece.
  chart->drawImage(solid(12, 24, Image::pack(0, 255, 0)), 12, 24);
  root.refresh();
  std::string all = enc.encode(b, root.images(), false);
  CHECK(count(all, "\x1bP") == 1 && all.find("\"1;1;12;24") != std::string::npos);
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

namespace {

// 16 columns of different greys, 10 levels apart, 12 rows.
Image greys() {
  Image img;
  img.width = 16;
  img.height = 12;
  for (int y = 0; y < 12; y++) {
    for (int x = 0; x < 16; x++) img.pixels.push_back(Image::pack(static_cast<uint8_t>(20 + 10 * x), static_cast<uint8_t>(20 + 10 * x), static_cast<uint8_t>(20 + 10 * x)));
  }
  return img;
}

}  // namespace

// The registers are the picture's own colours when they fit: a flat colour that is on no cube is flat.
TEST(sixel_images_with_few_colours_are_drawn_exactly_and_not_dithered) {
  Dither fs;
  fs.method = Dither::Method::FLOYD_STEINBERG;
  Image img;
  img.width = 12;
  img.height = 12;
  img.pixels.assign(144, Image::pack(20, 20, 20));
  std::vector<bool> keep(144, true);
  for (const Dither & d : {Dither{}, fs}) {
    std::string s = encodeSixel(img, 0, 0, 12, 12, keep, d);
    CHECK(count(s, ";2;") == 1 && count(s, "#0;2;8;8;8") == 1);  // 20 of 255, as the percent the protocol takes
  }
  // Every colour of a picture that fits has its own register, whichever dither.
  Image g = greys();
  std::vector<bool> all(16 * 12, true);
  std::string s = encodeSixel(g, 0, 0, 16, 12, all);
  CHECK(count(s, ";2;") == 16);
  CHECK(s == encodeSixel(g, 0, 0, 16, 12, all, fs));
}

// A Sixel colour has 101 levels a channel, so 256 greys are 101 colours: they fit 128 registers, and the
// flat colour among them is drawn as it is and not as a mean with its neighbours.
TEST(sixel_colours_the_terminal_cannot_tell_apart_are_one_register) {
  Image img;
  img.width = 256;
  img.height = 12;
  for (int y = 0; y < 12; y++) {
    for (int x = 0; x < 256; x++) {
      uint8_t v = y < 6 ? static_cast<uint8_t>(x) : 50;
      img.pixels.push_back(Image::pack(v, v, v));
    }
  }
  std::vector<bool> all(256 * 12, true);
  std::string s = encodeSixel(img, 0, 0, 256, 12, all, Dither{}, 128);
  size_t definitions = 0;  // "#<register>;2;": a grey of 2 percent holds ";2;" three times itself
  for (size_t i = s.find('#'); i != std::string::npos; i = s.find('#', i + 1)) {
    size_t j = i + 1;
    while (j < s.size() && s[j] >= '0' && s[j] <= '9') j++;
    if (j > i + 1 && s.compare(j, 3, ";2;") == 0) definitions++;
  }
  CHECK(definitions == 101);
  CHECK(count(s, ";2;20;20;20") == 1);  // 50 of 255
}

// More colours than registers: cut down to the registers, and the colours between them dithered.
TEST(sixel_images_with_more_colours_than_registers_use_no_more_registers_and_are_dithered) {
  Image g = greys();
  std::vector<bool> all(16 * 12, true);
  Dither nearest;
  nearest.method = Dither::Method::NEAREST;
  Dither fs;
  fs.method = Dither::Method::FLOYD_STEINBERG;
  for (const Dither & d : {Dither{}, nearest, fs}) CHECK(count(encodeSixel(g, 0, 0, 16, 12, all, d, 4), ";2;") <= 4);
  std::string ordered = encodeSixel(g, 0, 0, 16, 12, all, Dither{}, 4);
  CHECK(ordered != encodeSixel(g, 0, 0, 16, 12, all, nearest, 4));
  CHECK(encodeSixel(g, 0, 0, 16, 12, all, fs, 4) != encodeSixel(g, 0, 0, 16, 12, all, nearest, 4));
  // The same picture and settings make the same bytes.
  CHECK(ordered == encodeSixel(g, 0, 0, 16, 12, all, Dither{}, 4));
}

// The ordered pattern belongs to the screen, not the image: two tiles that
// meet line up, and a redraw keeps it.
TEST(ordered_dithering_follows_the_screen) {
  Image img = greys();
  std::vector<bool> keep(16 * 12, true);
  Dither at;
  at.origin_x = 3;
  at.origin_y = 5;
  Dither period = at;  // a whole matrix (8 pixels) on
  period.origin_x += 8;
  period.origin_y += 16;
  Dither shifted = at;
  shifted.origin_x += 1;
  std::string a = encodeSixel(img, 0, 0, 16, 12, keep, at, 3);
  CHECK(a == encodeSixel(img, 0, 0, 16, 12, keep, period, 3));
  CHECK(a != encodeSixel(img, 0, 0, 16, 12, keep, shifted, 3));
}

// Where a popup covers a chart only what shows is sent: a Sixel for each rectangle, not one for the
// box around them with the covered pixels left transparent.
TEST(sixel_images_send_each_visible_rectangle_and_not_the_box_around_them) {
  StyleProvider styles;
  CellBuffer b(4, 6);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(4, 6);
  chart->drawImage(solid(12, 24, Image::pack(255, 0, 0)), 12, 24);
  auto popup = root.createChild();
  popup->resize(2, 2);
  popup->move(1, 2);
  popup->putstr(0, 0, "ab");
  popup->putstr(1, 0, "cd");
  ImageEncoder enc(profile(PixelProtocol::SIXEL));
  root.refresh();
  std::string s = enc.encode(b, root.images(), true);
  CHECK(count(s, "\x1bP") >= 2);
  CHECK(s.find("\"1;1;12;18") == std::string::npos);  // the box around everything (three of the four rows)
  size_t pixels = 0;
  for (size_t at = s.find("\"1;1;"); at != std::string::npos; at = s.find("\"1;1;", at + 1)) {
    int w = 0, h = 0;
    std::sscanf(s.c_str() + at, "\"1;1;%d;%d", &w, &h);
    pixels += static_cast<size_t>(w * h);
  }
  CHECK(pixels == 12u * 18 - 4u * 12);  // the three rows shown, less the four cells the popup covers (2 x 6 pixels each)
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

namespace {

// The colour type, bit depth, palette and unpacked indices of a palette PNG (empty if it is not one).
struct PalettePng {
  int bits = 0, colour_type = -1;
  std::vector<uint32_t> palette;
  std::vector<int> indices;  // row by row, width of them each
};

PalettePng readPalettePng(const std::vector<uint8_t> & png, int width, int height) {
  PalettePng out;
  size_t i = 8;
  std::vector<uint8_t> idat;
  while (i + 8 <= png.size()) {
    uint32_t len = be32(png, i);
    std::string type(png.begin() + static_cast<long>(i + 4), png.begin() + static_cast<long>(i + 8));
    size_t data = i + 8;
    if (type == "IHDR") {
      out.bits = png[data + 8];
      out.colour_type = png[data + 9];
    } else if (type == "PLTE") {
      for (uint32_t k = 0; k + 2 < len; k += 3) out.palette.push_back((uint32_t(png[data + k]) << 16) | (uint32_t(png[data + k + 1]) << 8) | png[data + k + 2]);
    } else if (type == "IDAT") {
      auto z = inflate_test::Inflater(png.data() + data, len).zlib();
      if (z) idat = *z;
    }
    i = data + len + 4;
  }
  size_t row_bytes = (static_cast<size_t>(width) * static_cast<size_t>(out.bits) + 7) / 8;
  if (idat.size() < static_cast<size_t>(height) * (row_bytes + 1)) return out;  // not a stream we can read
  for (int y = 0; y < height; y++) {
    const uint8_t * row = idat.data() + static_cast<size_t>(y) * (row_bytes + 1) + 1;
    for (int x = 0; x < width; x++) {
      int v = out.bits == 8 ? row[x] : (x % 2 == 0 ? row[x / 2] >> 4 : row[x / 2] & 15);
      out.indices.push_back(v);
    }
  }
  return out;
}

}  // namespace

TEST(a_palette_png_of_few_colours_gives_them_back_exactly) {
  Image img;
  img.width = 5;
  img.height = 3;
  const uint32_t colours[3] = {Image::pack(10, 20, 30), Image::pack(200, 100, 50), Image::pack(0, 255, 0)};
  for (int k = 0; k < 15; k++) img.pixels.push_back(colours[(k * 7) % 3]);
  PalettePng p = readPalettePng(encodePalettePng(img, 0, 0, 5, 3, 64), 5, 3);
  CHECK(p.colour_type == 3 && p.bits == 4 && p.palette.size() == 3);  // 4 bits an index up to 16 colours
  bool same = p.indices.size() == 15;
  for (size_t k = 0; same && k < 15; k++) {
    uint32_t c = p.palette[static_cast<size_t>(p.indices[k])];
    same = Image::red(img.pixels[k]) == ((c >> 16) & 0xff) && Image::green(img.pixels[k]) == ((c >> 8) & 0xff) && Image::blue(img.pixels[k]) == (c & 0xff);
  }
  CHECK(same);
}

TEST(a_palette_png_of_more_colours_than_the_limit_has_no_more_than_the_limit) {
  Image img;
  img.width = 40;
  img.height = 8;
  for (int k = 0; k < 320; k++) img.pixels.push_back(Image::pack(static_cast<uint8_t>(k % 40 * 6), static_cast<uint8_t>(k / 40 * 30), 90));
  for (int limit : {16, 64}) {
    PalettePng p = readPalettePng(encodePalettePng(img, 0, 0, 40, 8, limit), 40, 8);
    CHECK(p.colour_type == 3 && p.palette.size() <= static_cast<size_t>(limit) && p.bits == (limit <= 16 ? 4 : 8));
    std::vector<uint8_t> rgb = quantisedRgb(img, limit);
    std::set<uint32_t> distinct;
    for (size_t k = 0; k + 2 < rgb.size(); k += 3) distinct.insert((uint32_t(rgb[k]) << 16) | (uint32_t(rgb[k + 1]) << 8) | rgb[k + 2]);
    CHECK(rgb.size() == 320u * 3 && distinct.size() <= static_cast<size_t>(limit));
  }
}

TEST(the_picture_quality_follows_the_link_class) {
  CHECK(pictureQualityFor(LinkClass::FULL) == PictureQuality::FULL && pictureQualityFor(LinkClass::HZ_30) == PictureQuality::FULL);
  CHECK(pictureQualityFor(LinkClass::HZ_15) == PictureQuality::REDUCED);
  CHECK(pictureQualityFor(LinkClass::HZ_5) == PictureQuality::LOW && pictureQualityFor(LinkClass::HZ_1) == PictureQuality::LOW);
  CHECK(pictureColours(PictureQuality::FULL) == 0 && pictureColours(PictureQuality::REDUCED) == 64 && pictureColours(PictureQuality::LOW) == 16);
}

TEST(kitty_sends_a_palette_png_at_reduced_quality_and_falls_back_when_the_terminal_refuses) {
  StyleProvider styles;
  CellBuffer b(22, 90);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(20, 80);
  // Large enough that the palette PNG is not weighed against the pixels.
  std::vector<uint32_t> px;
  for (int k = 0; k < 160 * 120; k++) px.push_back(Image::pack(static_cast<uint8_t>(k % 160 * 1), static_cast<uint8_t>(k / 160 * 2), static_cast<uint8_t>(k % 7 * 30)));
  chart->drawImage(px, 160, 120);
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  enc.setPictureQuality(PictureQuality::REDUCED);
  root.refresh();
  std::string first = enc.encode(b, root.images(), true);
  CHECK(count(first, "a=t,f=100,i=1,") == 1 && count(first, "f=24") == 0);
  // The payload is a PNG with a palette of at most 64 colours.
  std::string all_payload;
  for (size_t at = first.find("\x1b_G"); at != std::string::npos; at = first.find("\x1b_G", at + 1)) {
    size_t semi = first.find(';', at), end = first.find("\x1b\\", at);
    if (semi != std::string::npos && end != std::string::npos && semi < end) all_payload += first.substr(semi + 1, end - semi - 1);
  }
  std::vector<uint8_t> png = unbase64(all_payload);
  PalettePng p = readPalettePng(png, 160, 120);
  CHECK(png.size() > 8 && png[1] == 'P' && p.colour_type == 3 && p.palette.size() <= 64);
  // Refused: quantised RGB, deflated, from now on, and the same picture again.
  CHECK(!enc.handleKittyReply("i=1;EINVAL:bad format").empty());
  CHECK(!enc.palettePng());
  root.refresh();
  std::string again = enc.encode(b, root.images(), false);
  CHECK(count(again, "f=100") == 0 && count(again, "a=t,f=24,s=160,v=120,i=2,") == 1);
  // At full quality the pixels go as they are.
  ImageEncoder full(profile(PixelProtocol::KITTY));
  std::string plain = full.encode(b, root.images(), true);
  CHECK(count(plain, "f=24") == 1 && count(plain, "f=100") == 0);
}

// A PNG's headers and palette can outweigh what fewer colours save in a small picture: it is sent as
// the pixels then, so a lower quality is never larger.
TEST(a_small_picture_is_sent_in_the_smaller_of_its_palette_png_and_its_pixels) {
  StyleProvider styles;
  CellBuffer b(4, 4);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(2, 4);
  std::vector<uint32_t> px;
  for (int k = 0; k < 8 * 12; k++) px.push_back(Image::pack(static_cast<uint8_t>(k * 2), static_cast<uint8_t>(k % 8 * 30), 40));
  chart->drawImage(px, 8, 12);
  root.refresh();
  ImageEncoder full(profile(PixelProtocol::KITTY)), reduced(profile(PixelProtocol::KITTY));
  reduced.setPictureQuality(PictureQuality::REDUCED);
  size_t full_bytes = full.encode(b, root.images(), true).size(), reduced_bytes = reduced.encode(b, root.images(), true).size();
  CHECK(reduced_bytes <= full_bytes);
}

TEST(reduced_quality_makes_iterm2_and_sixel_pictures_smaller_with_fewer_colours) {
  StyleProvider styles;
  CellBuffer b(4, 12);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto chart = root.createChild();
  chart->resize(2, 12);
  std::vector<uint32_t> px;
  for (int k = 0; k < 24 * 12; k++) px.push_back(Image::pack(static_cast<uint8_t>(k % 24 * 10), static_cast<uint8_t>(k / 24 * 20), static_cast<uint8_t>(k * 7)));
  chart->drawImage(px, 24, 12);
  root.refresh();
  for (PixelProtocol protocol : {PixelProtocol::ITERM2, PixelProtocol::SIXEL}) {
    ImageEncoder full(profile(protocol)), low(profile(protocol));
    low.setPictureQuality(PictureQuality::LOW);
    size_t full_bytes = full.encode(b, root.images(), true).size(), low_bytes = low.encode(b, root.images(), true).size();
    CHECK(low_bytes > 0 && low_bytes < full_bytes);
  }
  ImageEncoder sixel(profile(PixelProtocol::SIXEL));
  sixel.setPictureQuality(PictureQuality::LOW);
  CHECK(count(sixel.encode(b, root.images(), true), ";2;") <= 16);
}

namespace {

// A smooth two-way ramp: red along, green down, blue with both; thousands of colours, a few levels apart.
Image ramp(int width, int height) {
  Image img;
  img.width = width;
  img.height = height;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      img.pixels.push_back(Image::pack(static_cast<uint8_t>(x * 255 / (width - 1)), static_cast<uint8_t>(y * 255 / (height - 1)),
                                       static_cast<uint8_t>((x + y) * 255 / (width + height - 2))));
    }
  }
  return img;
}

// The Sixel bytes for one picture at `quality`, with the profile's dither `method`.
std::string sixelAt(const Image & picture, PictureQuality quality, Dither::Method method) {
  ImageEncoder::Profile prof = profile(PixelProtocol::SIXEL);
  prof.dither = method;
  ImageEncoder enc(prof);
  enc.setPictureQuality(quality);
  int cols = picture.stretched() ? picture.cols : (picture.width + 1) / 2, rows = picture.stretched() ? picture.rows : (picture.height + 5) / 6;
  CellBuffer b(rows + 1, cols);
  CellBuffer::Cell marked;
  marked.image = 1;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) b.put(r, c, marked);
  }
  ImagePlacement place;
  place.image = std::make_shared<const Image>(picture);
  place.key = 1;
  return enc.encode(b, {place}, true);
}

}  // namespace

// A chart is not dithered below full quality (noise costs bytes a slow link has not got); at full quality
// the colours the registers cannot hold are.
TEST(sixel_is_not_dithered_below_full_quality) {
  Image chart = ramp(64, 60);
  CHECK(sixelAt(chart, PictureQuality::FULL, Dither::Method::ORDERED) != sixelAt(chart, PictureQuality::FULL, Dither::Method::NEAREST));
  for (PictureQuality q : {PictureQuality::REDUCED, PictureQuality::LOW}) {
    CHECK(sixelAt(chart, q, Dither::Method::ORDERED) == sixelAt(chart, q, Dither::Method::NEAREST));
  }
  CHECK(sixelAt(chart, PictureQuality::LOW, Dither::Method::ORDERED).size() < sixelAt(chart, PictureQuality::FULL, Dither::Method::ORDERED).size());
}

// A picture stretched over its cells (a blurred backdrop) is smooth and bands without dithering, so it
// keeps it at any quality.
TEST(a_stretched_picture_keeps_its_dither_on_a_slow_link) {
  Image backdrop = ramp(40, 12);
  backdrop.cols = 20;
  backdrop.rows = 2;
  for (PictureQuality q : {PictureQuality::REDUCED, PictureQuality::LOW}) {
    CHECK(sixelAt(backdrop, q, Dither::Method::ORDERED) != sixelAt(backdrop, q, Dither::Method::NEAREST));
  }
  // The palette PNG of kitty and iTerm2 follows the same rule.
  Image smooth = ramp(40, 12);
  CHECK(encodePalettePng(smooth, 0, 0, 40, 12, 4, true) != encodePalettePng(smooth, 0, 0, 40, 12, 4, false));
  CHECK(encodePalettePng(smooth, 0, 0, 40, 12, 4) == encodePalettePng(smooth, 0, 0, 40, 12, 4, false));
}

namespace {

std::vector<uint32_t> flat(int w, int h, uint8_t v) { return solid(w, h, Image::pack(v, v, v)); }

}  // namespace

// The same shared picture shown in several places is rendered once for the tree and uploaded once; each
// place is a placement of its own.
TEST(kitty_uploads_a_shared_picture_once_and_places_it_wherever_it_shows) {
  StyleProvider styles;
  CellBuffer b(4, 12);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto left = root.createChild(), right = root.createChild();
  left->resize(2, 6);
  right->resize(2, 6);
  right->move(0, 6);
  int rendered = 0;
  auto render = [&]() {
    rendered++;
    return flat(4, 12, 40);
  };
  left->drawSharedImageAt(0, 0, {"dial", 7}, 4, 12, render);
  left->drawSharedImageAt(0, 2, {"dial", 7}, 4, 12, render);
  right->drawSharedImageAt(0, 0, {"dial", 7}, 4, 12, render);
  CHECK(rendered == 1);
  root.refresh();
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  std::string first = enc.encode(b, root.images(), true);
  CHECK(count(first, "a=t,") == 1 && count(first, "a=p,i=1,") == 3);
  // Three placements of the one picture, each with a placement id of its own.
  std::set<std::string> ids;
  for (size_t at = first.find(",p="); at != std::string::npos; at = first.find(",p=", at + 1)) ids.insert(first.substr(at, first.find(',', at + 3) - at));
  CHECK(ids.size() == 3);
  root.refresh();
  CHECK(enc.encode(b, root.images(), false).empty());
  auto stats = enc.takePictureStats();
  CHECK(stats.count("dial") == 1 && stats["dial"].uploads == 1);
  // Another key is another picture.
  left->drawSharedImageAt(1, 0, {"dial", 8}, 4, 12, render);
  CHECK(rendered == 2);
  // A new cell size empties the registry: the pictures were made for the old one.
  root.setCellPixelSize(8, 4);
  left->drawSharedImageAt(1, 2, {"dial", 7}, 4, 12, render);
  CHECK(rendered == 3);
  root.invalidateSharedImages();
  right->drawSharedImageAt(1, 0, {"dial", 7}, 4, 12, render);
  CHECK(rendered == 4);
}

TEST(a_shared_picture_stays_in_the_terminal_after_its_last_placement_goes) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto plane = root.createChild();
  plane->resize(2, 8);
  auto render = []() { return flat(4, 12, 90); };
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  plane->drawSharedImageAt(0, 0, {"icon", 1}, 4, 12, render);
  plane->drawSharedImageAt(0, 2, {"icon", 1}, 4, 12, render);
  root.refresh();
  CHECK(count(enc.encode(b, root.images(), true), "a=t,") == 1);
  // One place goes: its placement only.
  plane->erase();
  plane->drawSharedImageAt(0, 0, {"icon", 1}, 4, 12, render);
  root.refresh();
  std::string one = enc.encode(b, root.images(), false);
  CHECK(count(one, "a=d,d=i,i=1,p=") == 1 && count(one, "d=I") == 0 && count(one, "a=t,") == 0);
  // The last goes: the data stays, and the picture shown again is placed, not sent.
  plane->erase();
  root.refresh();
  std::string none = enc.encode(b, root.images(), false);
  CHECK(count(none, "a=d,d=i,i=1,p=") == 1 && count(none, "d=I") == 0);
  plane->drawSharedImageAt(0, 4, {"icon", 1}, 4, 12, render);
  root.refresh();
  std::string again = enc.encode(b, root.images(), false);
  CHECK(count(again, "a=t,") == 0 && count(again, "a=p,i=1,") == 1);
}

TEST(shared_pictures_nothing_shows_are_freed_oldest_first_past_the_budget) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto plane = root.createChild();
  plane->resize(2, 8);
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  enc.setSharedBudget(2 * 4 * 12 * 4);  // two pictures of 4 x 12
  std::string sent;
  for (uint64_t id = 1; id <= 3; id++) {
    plane->erase();
    plane->drawSharedImageAt(0, 0, {"icon", id}, 4, 12, [&]() { return flat(4, 12, static_cast<uint8_t>(id * 40)); });
    root.refresh();
    sent += enc.encode(b, root.images(), id == 1);
  }
  // The third picture takes the terminal over the budget, and the first, the oldest nothing shows, goes.
  CHECK(count(sent, "a=d,d=I,i=1,") == 1 && count(sent, "d=I,i=2,") == 0 && count(sent, "d=I,i=3,") == 0);
}

TEST(a_place_that_changes_between_a_shared_picture_and_its_own_frees_only_what_is_its_own) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto plane = root.createChild();
  plane->resize(2, 8);
  ImageEncoder enc(profile(PixelProtocol::KITTY));
  auto render = []() { return flat(4, 12, 90); };
  plane->drawSharedImageAt(0, 0, {"icon", 1}, 4, 12, render);
  plane->drawSharedImageAt(0, 4, {"icon", 1}, 4, 12, render);
  root.refresh();
  enc.encode(b, root.images(), true);
  // The first place becomes a picture of its own: the shared one stays for the other.
  plane->drawImageAt(0, 0, flat(4, 12, 10), 4, 12);
  root.refresh();
  std::string changed = enc.encode(b, root.images(), false);
  CHECK(count(changed, "d=I,i=1") == 0 && count(changed, "a=t,") == 1);
}

// The registry is bounded: a dial dragged through every value makes a picture for each.
TEST(the_shared_picture_registry_is_bounded_and_the_places_keep_what_they_show) {
  StyleProvider styles;
  CellBuffer b(2, 4);
  BufferPlane root(b, styles);
  root.setCellPixelSize(6, 2);
  auto plane = root.createChild();
  plane->resize(1, 4);
  int rendered = 0;
  auto render = [&]() {
    rendered++;
    return flat(2, 6, 1);
  };
  plane->drawSharedImageAt(0, 0, {"dial", 0}, 2, 6, render);
  for (uint64_t id = 1; id <= 1100; id++) plane->drawSharedImageAt(0, 1, {"dial", id}, 2, 6, render);
  int before = rendered;
  // The one still shown at the first place is not made again; a key the registry let go of is.
  plane->drawSharedImageAt(0, 0, {"dial", 0}, 2, 6, render);
  CHECK(rendered == before);
  plane->drawSharedImageAt(0, 2, {"dial", 5}, 2, 6, render);
  CHECK(rendered == before + 1);
}

// A smooth dark gradient has more colours than registers, but on the terminal's 101 levels it would fit
// them and band: it is dithered between the levels, and a flat picture is not.
TEST(sixel_gradient_is_dithered_between_the_terminals_levels_and_a_flat_picture_is_not) {
  Image g;
  g.width = 300;
  g.height = 12;
  g.pixels.resize(300 * 12);
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 300; x++) g.pixels[static_cast<size_t>(y) * 300 + static_cast<size_t>(x)] = Image::pack(10 + x / 6, 10 + y + x / 20, 20 + x / 12);
  std::vector<bool> all(300 * 12, true);
  Dither nearest;
  nearest.method = Dither::Method::NEAREST;
  std::string banded = encodeSixel(g, 0, 0, 300, 12, all, nearest, 256);
  std::string dithered = encodeSixel(g, 0, 0, 300, 12, all, Dither{}, 256);
  CHECK(dithered != banded);
  // Between two levels the dither uses both, so it needs more of them than the plain mapping.
  CHECK(count(dithered, ";2;") >= count(banded, ";2;"));
  // Flat colour, however many ways it is asked for: one register, as before.
  Image flat;
  flat.width = 16;
  flat.height = 12;
  flat.pixels.assign(16 * 12, Image::pack(33, 33, 33));
  std::vector<bool> flat_all(16 * 12, true);
  CHECK(count(encodeSixel(flat, 0, 0, 16, 12, flat_all, Dither{}, 256), ";2;") == 1);
}

// A dark blurred backdrop spans a few levels: it has few colours, fewer than registers, yet bands at the
// terminal's 101 levels. Told it is a gradient (stretched over its cells) it is dithered; not told, the
// few colours are exact and nothing is.
TEST(sixel_gradient_with_few_colours_is_dithered_when_it_is_stretched) {
  Image g;
  g.width = 240;
  g.height = 12;
  g.pixels.resize(240 * 12);
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < 240; x++) g.pixels[static_cast<size_t>(y) * 240 + static_cast<size_t>(x)] = Image::pack(15 + x / 12, 20 + x / 12, 25 + x / 12);  // 20 greys-ish levels
  std::vector<bool> all(240 * 12, true);
  Dither plain;
  Dither gradient;
  gradient.gradient = true;
  std::string exact = encodeSixel(g, 0, 0, 240, 12, all, plain, 128);
  std::string dithered = encodeSixel(g, 0, 0, 240, 12, all, gradient, 128);
  CHECK(dithered != exact);
  // Between two grid levels it uses both: more registers than the 101 levels' snapping of 20 colours.
  CHECK(count(dithered, ";2;") >= count(exact, ";2;"));
  // Flat pictures stay as they are even when stretched: one register.
  Image flat;
  flat.width = 16;
  flat.height = 12;
  flat.pixels.assign(16 * 12, Image::pack(33, 33, 33));
  std::vector<bool> flat_all(16 * 12, true);
  CHECK(count(encodeSixel(flat, 0, 0, 16, 12, flat_all, gradient, 128), ";2;") == 1);
}
