#pragma once

#include <cellui/BackdropLayer.h>
#include <cellui/CellRasterizer.h>
#include <cellui/DisplayInfo.h>
#include <cellui/Image.h>
#include <cellui/LinkStatus.h>

#include <memory>
#include <vector>

namespace cellui::term {

// What a terminal shows of a backdrop. A terminal cannot blur what is below
// a layer, so this makes something of it: where it shows pictures, the cells
// below drawn as pictures, blurred and stretched over the backdrop's cells in
// tiles, redrawn only when what they show changed; otherwise each cell's
// colour averaged over its neighbours, its glyph fading into the average.
// Both are the terminal backend's own, made from the layers the application
// sets aside (BackdropLayer); a display that blurs for itself never sees
// them.
// How much work a backdrop is given, by what the link can carry: LIVE redraws the blur whenever what is
// below changes; STATIC keeps the blur as it was when the backdrop appeared (until it goes); CELLS
// blurs the cells, never as pictures (and keeps that too); FLAT only dims.
enum class BackdropMode { LIVE, STATIC, CELLS, FLAT };

// With pictures the blur is live at full speed, static from 30 Hz, cells at 5 Hz and flat at 1 Hz;
// without them the cell blur is live to 30 Hz, static at 15 and 5 Hz, and flat at 1 Hz.
BackdropMode backdropModeFor(LinkClass cls, bool pictures);

class BackdropEmulation {
 public:
  // `rasterizer` draws the cells as pictures; without one, one is made
  // when the display shows pictures.
  explicit BackdropEmulation(std::shared_ptr<CellRasterizer> rasterizer = nullptr);

  // Whether there is anything kept from an earlier frame.
  bool holdsState() const { return !tiles_.empty() || !filtered_.empty() || !regions_.empty(); }

  void setMode(BackdropMode mode) { mode_ = mode; }

  // `frame` as the terminal shows it: the layers, bottom first, each with
  // its backdrop applied to what is composed below it, and the frame's own
  // cells above. `images` gains the pictures the tiles are. With no layers
  // the frame stays as it is.
  void apply(CellBuffer & frame, std::vector<ImagePlacement> & images, const std::vector<BackdropLayer> & layers,
             const DisplayInfo & display);

  // How many tiles the last apply() drew anew.
  int redrawnTiles() const { return redrawn_tiles_; }

 private:
  struct Tile {
    int row = 0, col = 0, rows = 0, cols = 0;
    uint64_t hash = 0, key = 0;
    Backdrop backdrop;
    std::shared_ptr<const Image> image;
    bool used = false;
  };
  // Images as a backdrop shows them, kept while they are shown so that one
  // is filtered and sent once.
  struct Filtered {
    uint64_t key = 0, source_version = 0;
    Backdrop backdrop;
    std::shared_ptr<const Image> image;
    bool used = false;
  };

  // The cells a cell-blurred backdrop gave, kept while it is static.
  struct Region {
    int row = 0, col = 0, rows = 0, cols = 0;
    Backdrop backdrop;
    CellBuffer cells{ 0, 0 };
    bool used = false;
  };

  void applyLayer(CellBuffer & out, std::vector<ImagePlacement> & images, const BackdropLayer & layer, const DisplayInfo & display);
  int tile(const CellBuffer & below, std::vector<ImagePlacement> & images, int row, int col, int rows, int cols, const Backdrop & backdrop,
           const DisplayInfo & display);
  std::shared_ptr<const Image> filtered(const ImagePlacement & placement, const Backdrop & backdrop, const DisplayInfo & display);

  std::shared_ptr<CellRasterizer> rasterizer_;
  std::vector<Tile> tiles_;
  std::vector<Filtered> filtered_;
  std::vector<Region> regions_;
  BackdropMode mode_ = BackdropMode::LIVE;
  int redrawn_tiles_ = 0;
  // Keys and versions of what is made here, apart from the application's.
  uint64_t next_key_ = uint64_t{ 1 } << 48, next_version_ = uint64_t{ 1 } << 48;
};

}  // namespace cellui::term
