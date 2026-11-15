#pragma once

#include <cellui/Plane.h>

#include <climits>
#include <cstdint>
#include <vector>

namespace cellui {

// What a painter draws: either 8-bit pixels (R, G, B, A in memory order, width*height of them) or, when
// the display asked for them and the canvas backend has float surfaces, floats (R, G, B, A in linear
// Rec.2020, 1.0 = SDR white, width*height*4 of them). The painters draw the same colours either way;
// on an 8-bit canvas, canvas itself maps each colour to sRGB.
struct Picture {
  int width = 0, height = 0;
  std::vector<uint32_t> pixels;
  std::vector<float> hdr;
  bool isHdr() const { return !hdr.empty(); }
};

// Shows the picture over the whole plane (Plane::drawHdrImage() or drawImage()).
inline void drawPicture(Plane & plane, const Picture & picture, uint64_t id = 0) {
  if (picture.isHdr()) plane.drawHdrImage(picture.hdr, picture.width, picture.height, id);
  else plane.drawImage(picture.pixels, picture.width, picture.height, id);
}

// The same from cell (row, col), as Plane::drawImageAt().
inline void drawPictureAt(Plane & plane, int row, int col, const Picture & picture, uint64_t id = 0, int clip_top = INT_MIN,
                          int clip_bottom = INT_MAX) {
  if (picture.isHdr()) plane.drawHdrImageAt(row, col, picture.hdr, picture.width, picture.height, id, clip_top, clip_bottom);
  else plane.drawImageAt(row, col, picture.pixels, picture.width, picture.height, id, clip_top, clip_bottom);
}

}  // namespace cellui
