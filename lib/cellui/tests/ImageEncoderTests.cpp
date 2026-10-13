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
