#pragma once

#include <cellui/Image.h>

namespace cellui {

// The picture resampled to width x height: bilinear, with the source pixels' centres where a pixel of the
// result is centred over them and the edge pixels extended outward. The one scaler for every picture that is
// drawn small and enlarged (a blurred backdrop stretched over its cells, the heatmap's grid).
Image scaleImage(const Image & src, int width, int height);

}  // namespace cellui
