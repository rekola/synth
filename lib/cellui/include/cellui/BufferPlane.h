#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/CellRasterizer.h>
#include <cellui/Image.h>
#include <cellui/LineEditor.h>
#include <cellui/PlaneInfo.h>
#include <cellui/UIPlane.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cellui {

// A plane that keeps its own cells in memory instead of drawing on a screen,
// the way a screen's planes do: drawing, moving, resizing, hiding and raising
// a plane work on the plane, and refresh() on any plane of the tree composes
// them into the root's CellBuffer - children over their parent in the order
// they were created or raised, a cell nothing was written to showing what is
// below it, every child clipped to its parent. A translucent cell
// (setBgBlend()) averages its background with what is below, and a backdrop
// cell (setBackdrop()) shows what is below blurred and dimmed, images
// included: an image under one is shown filtered, whole.
//
// It is what the terminal backend draws: the terminal shows the composed
// buffer. So it also has the reader (a one-line field over its own child
// plane, edited with LineEditor's keys, its cursor reported by
// cursorPosition()), the reader's indicator, and the picker (a boxed list
// in a child plane, moved with the arrows, PgUp/PgDn, the wheel and a
// click). Widgets can be laid out, drawn and read back in a test, or
// printed by the gallery.
class BufferPlane : public UIPlane {
 public:
  // The root plane covers the whole buffer.
  BufferPlane(CellBuffer & target, const StyleProvider & styles, std::shared_ptr<void> app_context = nullptr);
  ~BufferPlane() override;
  BufferPlane(const BufferPlane &) = delete;
  BufferPlane & operator=(const BufferPlane &) = delete;

  void resize(int rows, int cols) override;
  void moveToTop() override;
  void setVisible(bool visible) override { visible_ = visible; }
  bool isVisible() const { return visible_; }
  void setName(const std::string & name) override { name_ = name; }
  // This plane and the planes on it, as they are now.
  PlaneInfo describe() const;

  void setFgColor(int r, int g, int b) override { style_.fg = Color(r, g, b); }
  void setBgColor(int r, int g, int b) override { style_.bg = Color(r, g, b); }
  void setBgDefault() override {
    style_.bg.reset();
    base_bg_.reset();
  }
  // Where nothing is written the plane shows what is below it, glyph and
  // colours alike, even with a themed base.
  void makeTransparent() override { base_bg_.reset(); }
  void setUnderline(bool b) override { style_.underline = b; }
  void setBold(bool b) override { style_.bold = b; }
  void setItalic(bool b) override { style_.italic = b; }
  void setBgBlend(bool b) override { style_.bg_blend = b; }
  void setFgContrast(bool b) override { style_.fg_contrast = b; }
  void setBackdrop(const Backdrop & b) override { style_.backdrop = b; }
  void erase() override {
    local_.clear(0, 0, local_.rows(), local_.cols());
    // Kept, so the same picture drawn again after erase() is not sent again.
    erased_images_ = std::move(images_);
    images_.clear();
  }
  void putstr(int y, int x, const std::string & s) override;
  std::unique_ptr<UIPlane> createChild() override;
  void drawBorder() override;
  // Feeds the reader or the picker, whichever is open.
  bool offerInput(const Input & input) override;
  // Composes the whole tree into the root's buffer.
  void refresh() override;

  void showReader(const std::string & prompt, int y, int x, int rows, int cols, const std::string & initial_text,
                  std::optional<Color> text_color) override;
  std::string closeReader() override;
  bool readerActive() const override { return editor_ != nullptr; }
  std::string getReaderContents() const override { return editor_ ? editor_->text() : ""; }
  void setReaderContents(const std::string & text) override;
  // Inserts text at the reader's cursor (a paste).
  void insertReaderText(const std::string & text);
  // The same, into whichever reader of the tree is open; newlines become
  // spaces, as a field is one line. Returns whether one was (root only).
  bool insertIntoReader(const std::string & text);
  void showReaderIndicator(int x, const std::string & s) override;
  void hideReaderIndicator() override { indicator_.reset(); }

  void showPicker(int y, int x, int rows, int cols, int item_count) override;
  void addItem(const std::string & id, const std::string & description) override;
  void selectPickerItem(int index) override;
  bool pickerActive() const override { return picker_ != nullptr; }
  std::string getPickerSelection() const override;
  void closePicker() override;

  // The colours a translucent cell over nothing blends with: the terminal's
  // own (root only).
  void setDefaultColors(Color fg, Color bg) {
    default_fg_ = fg;
    default_bg_ = bg;
  }
  bool pixelSize(int & height, int & width) const override;
  bool cellPixelSize(int & height, int & width) const override;
  void drawImage(const std::vector<uint32_t> & pixels, int width, int height, uint64_t id = 0) override;
  void drawImageAt(int row, int col, const std::vector<uint32_t> & pixels, int width, int height, uint64_t id = 0, int clip_top = INT_MIN,
                   int clip_bottom = INT_MAX) override;
  // The cell size in pixels, 0 when pixels cannot be shown (root only).
  void setCellPixelSize(int height, int width) {
    cell_px_h_ = height;
    cell_px_w_ = width;
  }
  // The images the last refresh() composed, bottom to top (root only).
  const std::vector<ImagePlacement> & images() const { return placements_; }
  // With flat children, createChild() on any plane of the tree makes a plane
  // of the root's, placed in screen coordinates and stacked by creation
  // order, not one nested in (and clipped to) the plane it was asked of -
  // the model the terminal application's widgets were written for, where a
  // widget's popup can rise above any other widget (root only).
  void setFlatChildren(bool flat) { flat_ = flat; }
  // With themed bases, a plane made by createChild() shows the theme's
  // plane base background (under the glyph below) wherever it writes
  // nothing, until makeTransparent() (root only).
  void setThemedBases(bool themed) { themed_bases_ = themed; }
  // With a rasterizer and the cell size in pixels, a blurring backdrop is
  // shown as pictures of the cells below it drawn, blurred and stretched
  // over its cells, in tiles redrawn only when what they show changed
  // (root only).
  void setRasterizer(std::shared_ptr<CellRasterizer> rasterizer) { rasterizer_ = std::move(rasterizer); }
  // With a native backdrop, a backdrop is not applied to the cells: what
  // lies below it is kept apart as a layer (takeBackdropLayers()) and the
  // frame holds only what is above it (root only).
  void setNativeBackdrop(bool native) { native_backdrop_ = native; }
  // The layers the last refresh() set aside, bottom first (root only).
  std::vector<BackdropLayer> takeBackdropLayers() { return std::move(layers_); }
  // How many tiles the last refresh() drew anew (root only).
  int redrawnBackdropTiles() const { return redrawn_tiles_; }
  // What canRenderSextants() reports, for the whole tree (root only).
  void setSextants(bool can) { sextants_ = can; }
  bool canRenderSextants() const override { return root().sextants_; }
  // What displayScale() reports, for the whole tree (root only).
  void setDisplayScale(double scale) { display_scale_ = scale; }
  double displayScale() const override { return root().display_scale_; }
  // Where the terminal's cursor goes: in the open reader, if any (root only).
  std::optional<std::pair<int, int>> cursorPosition() const;
  // This plane's top-left corner on the screen.
  std::pair<int, int> screenPosition() const;
  // The cells as written, before composition.
  const CellBuffer & cells() const { return local_; }

 private:
  struct Picker;

  explicit BufferPlane(BufferPlane & parent);
  std::unique_ptr<BufferPlane> makeChild() { return std::unique_ptr<BufferPlane>(new BufferPlane(*this)); }

  BufferPlane & root() { return parent_ ? parent_->root() : *this; }
  const BufferPlane & root() const { return parent_ ? parent_->root() : *this; }
  // Draws this plane and its children onto `out` with the plane's top-left
  // corner at (row, col), inside the clip rectangle.
  void compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols) const;
  bool shown() const;
  // The placement's image as `backdrop` shows it (root only).
  std::shared_ptr<const Image> filteredImage(const ImagePlacement & placement, const Backdrop & backdrop) const;
  // Places the backdrop picture of screen cells [row, row + rows) x
  // [col, col + cols) over `below`, drawn anew only if what it shows changed,
  // and returns its placement's index (root only).
  int backdropTile(const CellBuffer & below, int row, int col, int rows, int cols, const Backdrop & backdrop) const;
  void drawReader();
  void drawPicker();

  CellBuffer local_;
  CellBuffer * target_ = nullptr;  // the root's output
  BufferPlane * parent_ = nullptr;
  std::vector<BufferPlane *> children_;  // bottom to top
  CellBuffer::Style style_;
  bool visible_ = true;
  std::string name_;

  // The reader: the text, the plane it shows in, its colour and column.
  std::unique_ptr<LineEditor> editor_;
  std::unique_ptr<BufferPlane> reader_plane_;
  Color reader_color_;
  int reader_x_ = 0;
  int reader_cursor_ = 0;
  std::unique_ptr<BufferPlane> indicator_;
  std::unique_ptr<Picker> picker_;

  // Root only.
  BufferPlane * active_reader_ = nullptr;
  Color default_fg_ = Color(255, 255, 255), default_bg_ = Color(0, 0, 0);
  double display_scale_ = 1.0;
  bool sextants_ = false;
  bool native_backdrop_ = false;
  mutable std::vector<BackdropLayer> layers_;
  bool flat_ = false;
  bool themed_bases_ = false;
  int cell_px_h_ = 0, cell_px_w_ = 0;
  mutable std::vector<ImagePlacement> placements_;
  // Images as a backdrop shows them, kept while they are shown so that one
  // is filtered and sent once.
  struct FilteredImage {
    uint64_t key = 0, source_version = 0;
    Backdrop backdrop;
    std::shared_ptr<const Image> image;
    bool used = false;
  };
  mutable std::vector<FilteredImage> filtered_images_;
  std::shared_ptr<CellRasterizer> rasterizer_;
  struct BackdropTile {
    int row = 0, col = 0, rows = 0, cols = 0;
    uint64_t hash = 0, key = 0;
    Backdrop backdrop;
    std::shared_ptr<const Image> image;
    bool used = false;
  };
  mutable std::vector<BackdropTile> backdrop_tiles_;
  mutable int redrawn_tiles_ = 0;

  // The background of the cells nothing was written to; unset shows below.
  std::optional<Color> base_bg_;
  // The images shown over the plane's cells, by cell position, each under
  // a key of its own so the backend can tell a moved or redrawn image apart.
  struct PlacedImage {
    int row = 0, col = 0;
    std::shared_ptr<Image> image;
    uint64_t key = 0;
    uint64_t identity = 0;  // the caller's id, or a hash of the pixels
    bool explicit_id = false;
    int clip_top = INT_MIN, clip_bottom = INT_MAX;  // plane rows it shows in
  };
  std::vector<PlacedImage> images_;
  std::vector<PlacedImage> erased_images_;  // what erase() last took away
};

}  // namespace cellui
