#pragma once

#include <cellui/Scopes.h>
#include <cellui/term/Capabilities.h>

#include <memory>

namespace cellui::term {

// The scopes a terminal shows: as images when it has an image protocol and
// its cell size in pixels is known, else drawn in cells.
inline std::unique_ptr<Scopes> scopes(const Capabilities & caps) {
  if (caps.pixels != PixelProtocol::NONE && caps.cell_px_height > 0 && caps.cell_px_width > 0) {
    if (auto pixel = pixelScopes()) return pixel;
  }
  return cellScopes();
}

}  // namespace cellui::term
