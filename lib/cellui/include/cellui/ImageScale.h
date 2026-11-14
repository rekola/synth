#pragma once

#include <cellui/Image.h>

namespace cellui {

// The picture resampled to width x height: bilinear, with the source pixels' centres where a pixel of the
// result is centred over them and the edge pixels extended outward. For a picture that is smooth and drawn
// small and enlarged (a blurred backdrop stretched over its cells).
Image scaleImage(const Image & src, int width, int height);

// The same size by nearest neighbour: each source pixel becomes a block of one colour (runs that compress well,
// cells with hard edges). For a grid whose cells are meant to show (the heatmap).
Image scaleImageNearest(const Image & src, int width, int height);

}  // namespace cellui
