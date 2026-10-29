#include <cellui/Scopes.h>

#include <cellui/CellRasterizer.h>

namespace cellui {

#ifndef CELLUI_PIXEL_SCOPES
std::unique_ptr<Scopes> pixelScopes() { return nullptr; }
#endif
// Windows draws its cells itself and has no cell rasterizer.
#ifndef CELLUI_CANVAS_RASTERIZER
std::unique_ptr<CellRasterizer> canvasRasterizer(const std::string &, bool) { return nullptr; }
#endif

}  // namespace cellui
