#include <cellui/gtk/Window.h>

#include <cellui/CellRasterizer.h>
#include <cellui/gtk/GlibEventLoop.h>

#include <gtk/gtk.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <set>

namespace cellui::gtk {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

GskBlendMode gskBlendMode(BlendMode mode) {
  switch (mode) {
    case BlendMode::MULTIPLY: return GSK_BLEND_MODE_MULTIPLY;
    case BlendMode::SCREEN: return GSK_BLEND_MODE_SCREEN;
    case BlendMode::OVERLAY: return GSK_BLEND_MODE_OVERLAY;
    case BlendMode::DARKEN: return GSK_BLEND_MODE_DARKEN;
    case BlendMode::LIGHTEN: return GSK_BLEND_MODE_LIGHTEN;
    case BlendMode::DIFFERENCE: return GSK_BLEND_MODE_DIFFERENCE;
    case BlendMode::EXCLUSION: return GSK_BLEND_MODE_EXCLUSION;
    case BlendMode::HUE: return GSK_BLEND_MODE_HUE;
    case BlendMode::SATURATION: return GSK_BLEND_MODE_SATURATION;
    case BlendMode::COLOR: return GSK_BLEND_MODE_COLOR;
    case BlendMode::LUMINOSITY: return GSK_BLEND_MODE_LUMINOSITY;
    case BlendMode::NORMAL: break;
  }
  return GSK_BLEND_MODE_DEFAULT;
}

unsigned modsFrom(GdkModifierType state) {
  unsigned mods = 0;
  if (state & GDK_SHIFT_MASK) mods |= kModShift;
  if (state & GDK_CONTROL_MASK) mods |= kModCtrl;
  if (state & GDK_ALT_MASK) mods |= kModAlt;
  if (state & GDK_SUPER_MASK) mods |= kModSuper;
  if (state & GDK_META_MASK) mods |= kModMeta;
  return mods;
}

// The key id for a GDK keyval: a codepoint for a key that produces a
// character, a Key:: id for the others, 0 for one that is ignored (a
// modifier on its own).
int keyFor(guint keyval, unsigned & mods) {
  if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F35) return Key::F1 + static_cast<int>(keyval - GDK_KEY_F1);
  if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9) return '0' + static_cast<int>(keyval - GDK_KEY_KP_0);
  switch (keyval) {
    case GDK_KEY_Up: case GDK_KEY_KP_Up: return Key::Up;
    case GDK_KEY_Down: case GDK_KEY_KP_Down: return Key::Down;
    case GDK_KEY_Left: case GDK_KEY_KP_Left: return Key::Left;
    case GDK_KEY_Right: case GDK_KEY_KP_Right: return Key::Right;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: return Key::Home;
    case GDK_KEY_End: case GDK_KEY_KP_End: return Key::End;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: return Key::PageUp;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return Key::PageDown;
    case GDK_KEY_Insert: case GDK_KEY_KP_Insert: return Key::Insert;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete: return Key::Delete;
    case GDK_KEY_BackSpace: return Key::Backspace;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter: return Key::Enter;
    case GDK_KEY_Tab: return Key::Tab;
    case GDK_KEY_ISO_Left_Tab:
      mods |= kModShift;
      return Key::Tab;
    case GDK_KEY_Escape: return Key::Escape;
    case GDK_KEY_Menu: return Key::Menu;
    case GDK_KEY_KP_Divide: return Key::KpDivide;
    case GDK_KEY_KP_Multiply: return Key::KpMultiply;
    case GDK_KEY_KP_Add: return '+';
    case GDK_KEY_KP_Subtract: return '-';
    case GDK_KEY_KP_Decimal: return '.';
    case GDK_KEY_KP_Space: return ' ';
    default: break;
  }
  // Modifiers, locks and the other keys that make no character.
  if (keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) return 0;
  if (keyval == GDK_KEY_ISO_Level3_Shift || keyval == GDK_KEY_ISO_Level5_Shift || keyval == GDK_KEY_Mode_switch) return 0;
  return static_cast<int>(gdk_keyval_to_unicode(keyval));
}

}  // namespace

// The widget: a GtkWidget subclass whose snapshot() shows the frame.
struct _CelluiView {
  GtkWidget parent;
  Window::Impl * impl;
};
struct _CelluiViewClass {
  GtkWidgetClass parent_class;
};
using CelluiView = _CelluiView;
using CelluiViewClass = _CelluiViewClass;

}  // namespace cellui::gtk

namespace cellui::gtk {

struct Window::Impl {
  Window * owner = nullptr;
  GlibEventLoop loop;
  GtkWidget * window = nullptr;
  GtkWidget * view = nullptr;
  std::unique_ptr<CellRasterizer> rasterizer;

  DisplayInfo display_info;
  int rows = 0, cols = 0;
  int cell_h = 0, cell_w = 0;

  int input_fd = -1, wake_fd = -1;
  std::deque<BackendEvent> queue;

  // The frame being shown: what lies above the backdrops, and the layers
  // below each of them.
  CellBuffer frame{ 0, 0 };
  CursorRequest cursor;
  std::vector<ImagePlacement> images;
  std::vector<BackdropLayer> layers;
  std::vector<ElevatedArea> elevations;
  // The areas planes asked a pointer shape for, and the one now set.
  std::vector<CursorArea> mouse_cursors;
  MouseCursor shown_cursor = MouseCursor::DEFAULT;
  // The texture made of each layer's cells, and of the frame's, kept while
  // what they show is unchanged (a texture that stays the same is not
  // uploaded again).
  struct Painted {
    uint64_t print = 0;
    GdkTexture * texture = nullptr;
    int row = 0, col = 0;  // the cell the texture's corner is at
    // A layer is kept as bands of rows, each with its own texture, so a
    // change in a few rows (the scopes, while playing) draws only those.
    std::vector<Painted> bands;
    bool valid = false;  // a band that has been painted (it may hold no texture)
  };
  std::vector<Painted> painted;
  FrameStats stats;


  std::set<guint> held_keys;
  int held_button = 0;
  int last_row = -1, last_col = -1;
  // The pointer's position in cells, with the fraction.
  float last_fy = -1, last_fx = -1;
  double scroll_x = 0, scroll_y = 0;

  void signal(int fd) {
    uint64_t one = 1;
    (void)!::write(fd, &one, sizeof one);
  }
  void drain(int fd) {
    uint64_t count;
    (void)!::read(fd, &count, sizeof count);
  }
  void push(BackendEvent event) {
    queue.push_back(std::move(event));
    signal(input_fd);
  }
  // The cell under a pixel, clamped to the grid.
  void cellAt(double x, double y, int & row, int & col) const {
    col = std::clamp(static_cast<int>(std::floor(x / cell_w)), 0, std::max(0, cols - 1));
    row = std::clamp(static_cast<int>(std::floor(y / cell_h)), 0, std::max(0, rows - 1));
  }

  // The position under a pixel in cells, with the fraction, clamped to the grid.
  void cellPosition(double x, double y, float & fy, float & fx) const {
    float max_x = std::max(0.0f, static_cast<float>(cols) - 0.001f), max_y = std::max(0.0f, static_cast<float>(rows) - 0.001f);
    fx = std::clamp(static_cast<float>(x / cell_w), 0.0f, max_x);
    fy = std::clamp(static_cast<float>(y / cell_h), 0.0f, max_y);
  }

  void measure(const WindowOptions & options);
  void snapshot(GtkSnapshot * snapshot);
  const Painted & paint(size_t slot, const CellBuffer & cells, bool transparent, bool with_images = true);
  void appendTexture(GtkSnapshot * snapshot, const Painted & painted, int row = 0, int col = 0) const;
  static void release(Painted & p) {
    if (p.texture) g_object_unref(p.texture);
    for (Painted & band : p.bands) release(band);
    p = {};
  }
  void appendShadows(GtkSnapshot * snap, const Painted & top, GskRenderNode * below);
  void forgetTextures() {
    for (Painted & p : painted) release(p);
  }
  void allocated(int width, int height);
  void pasteClipboard();
  bool keyEvent(guint keyval, GdkModifierType state, bool pressed, guint keycode);
  void buttonEvent(int button, double x, double y, bool pressed, GdkModifierType state);
  void motionEvent(double x, double y, GdkModifierType state);
  void updateMouseCursor();
  bool scrollEvent(double dx, double dy, GdkModifierType state);
};

}  // namespace cellui::gtk

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"

using cellui::gtk::CelluiView;
using cellui::gtk::CelluiViewClass;

G_DEFINE_TYPE(CelluiView, cellui_view, GTK_TYPE_WIDGET)

static void cellui_view_snapshot(GtkWidget * widget, GtkSnapshot * snapshot) {
  auto * self = reinterpret_cast<CelluiView *>(widget);
  if (self->impl) self->impl->snapshot(snapshot);
}

static void cellui_view_size_allocate(GtkWidget * widget, int width, int height, int) {
  auto * self = reinterpret_cast<CelluiView *>(widget);
  if (self->impl) self->impl->allocated(width, height);
}

static void cellui_view_measure(GtkWidget * widget, GtkOrientation orientation, int, int * minimum, int * natural, int * minimum_baseline,
                                int * natural_baseline) {
  auto * self = reinterpret_cast<CelluiView *>(widget);
  int cells = 1, wanted = 1;
  if (self->impl) {
    bool horizontal = orientation == GTK_ORIENTATION_HORIZONTAL;
    int cell = horizontal ? self->impl->cell_w : self->impl->cell_h;
    cells = horizontal ? 20 : 6;
    wanted = (horizontal ? self->impl->cols : self->impl->rows);
    *minimum = cells * cell;
    *natural = wanted * cell;
  } else {
    *minimum = *natural = 1;
  }
  *minimum_baseline = *natural_baseline = -1;
}

static void cellui_view_class_init(CelluiViewClass * klass) {
  auto * widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->snapshot = cellui_view_snapshot;
  widget_class->size_allocate = cellui_view_size_allocate;
  widget_class->measure = cellui_view_measure;
}

static void cellui_view_init(CelluiView * self) {
  self->impl = nullptr;
  gtk_widget_set_focusable(GTK_WIDGET(self), TRUE);
  gtk_widget_set_can_focus(GTK_WIDGET(self), TRUE);
}

#pragma GCC diagnostic pop

namespace cellui::gtk {

void Window::Impl::measure(const WindowOptions & options) {
  PangoFontDescription * desc = pango_font_description_from_string(options.font.c_str());
  PangoContext * context = gtk_widget_create_pango_context(view);
  PangoLayout * layout = pango_layout_new(context);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, "M", -1);
  PangoRectangle logical;
  pango_layout_get_pixel_extents(layout, nullptr, &logical);
  cell_w = std::max(logical.width, 4);
  cell_h = std::max(logical.height, 8);
  const char * family = pango_font_description_get_family(desc);
  rasterizer = canvasRasterizer(family ? family : "monospace");
  g_object_unref(layout);
  g_object_unref(context);
  pango_font_description_free(desc);

  // The colours cells without one are drawn in follow the theme's text; the
  // background is dark when that text is light.
  GdkRGBA text;
  gtk_widget_get_color(view, &text);
  auto byte = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  Color fg(byte(text.red), byte(text.green), byte(text.blue));
  float luminance = 0.2126f * text.red + 0.7152f * text.green + 0.0722f * text.blue;
  Color bg = luminance > 0.5f ? Color(0x1e, 0x1e, 0x1e) : Color(0xfa, 0xfa, 0xfa);
  display_info.cell_px_height = cell_h;
  display_info.cell_px_width = cell_w;
  display_info.pictures = rasterizer != nullptr;
  display_info.default_fg = fg;
  display_info.default_bg = bg;
  display_info.display_scale = 1.0;
  display_info.sextants = false;
  display_info.smooth_scroll = true;
  display_info.keymap_profile = KeymapProfile::DESKTOP;
  display_info.escape_as_meta = false;
  display_info.key_release = true;
  display_info.precise_mouse = true;
  display_info.hover = true;
}

void Window::Impl::allocated(int width, int height) {
  int new_cols = std::max(1, width / cell_w), new_rows = std::max(1, height / cell_h);
  if (new_cols == cols && new_rows == rows) return;
  cols = new_cols;
  rows = new_rows;
  push(BackendEvent::makeResize(rows, cols));
}

namespace {

// What cells and pictures look like, as one number.
class Print {
 public:
  void add(uint64_t v) { value_ = (value_ ^ v) * 1099511628211ull; }
  void add(const std::string & text) {
    for (char c : text) add(static_cast<uint64_t>(static_cast<unsigned char>(c)));
    add(static_cast<uint64_t>(text.size()));
  }
  void add(const std::optional<Color> & c) {
    uint64_t packed = 0;
    if (c) {
      auto rgb = c->toSrgb8();
      packed = uint64_t{ 1 } << 32 | static_cast<uint64_t>(rgb[0]) << 16 | static_cast<uint64_t>(rgb[1]) << 8 | static_cast<uint64_t>(rgb[2]);
    }
    add(packed);
  }
  uint64_t value() const { return value_; }

 private:
  uint64_t value_ = 14695981039346656037ull;
};

// Rows [r0, r1) of the cells, and the pictures they show.
uint64_t printOf(const CellBuffer & cells, const std::vector<ImagePlacement> & images, int r0, int r1, bool transparent, bool with_images) {
  Print print;
  print.add(static_cast<uint64_t>(r0) << 32 | static_cast<uint32_t>(cells.cols()));
  print.add(transparent);
  std::set<int> shown;
  for (int r = r0; r < r1; r++) {
    for (int c = 0; c < cells.cols(); c++) {
      const CellBuffer::Cell & cell = cells.at(r, c);
      print.add(cell.text);
      print.add(cell.fg);
      print.add(cell.bg);
      print.add(static_cast<uint64_t>(cell.bold) | static_cast<uint64_t>(cell.italic) << 1 | static_cast<uint64_t>(cell.underline) << 2 |
                static_cast<uint64_t>(cell.continuation) << 3 | static_cast<uint64_t>(cell.image) << 4);
      if (with_images && cell.image > 0) shown.insert(cell.image);
    }
  }
  for (int index : shown) {
    if (static_cast<size_t>(index) > images.size()) continue;
    const ImagePlacement & p = images[static_cast<size_t>(index - 1)];
    print.add(p.key);
    print.add(p.image ? p.image->version : 0);
    print.add(static_cast<uint64_t>(p.row) << 32 | static_cast<uint32_t>(p.col));
  }
  return print.value();
}

}  // namespace

// The textures of a layer's cells, kept as bands of rows and drawn again only
// for a band whose cells or pictures changed (the scopes, while playing, are
// a few bands). A transparent layer's band holds only the rectangle its
// non-blank cells span, with the blank cells in it see-through.
const Window::Impl::Painted & Window::Impl::paint(size_t slot, const CellBuffer & cells, bool transparent, bool with_images) {
  if (painted.size() <= slot) painted.resize(slot + 1);
  Painted & p = painted[slot];
  constexpr int kBandRows = 4;
  size_t count = static_cast<size_t>((cells.rows() + kBandRows - 1) / kBandRows);
  uint64_t shape = static_cast<uint64_t>(cells.rows()) << 32 | static_cast<uint32_t>(cells.cols());
  if (p.bands.size() != count || p.print != shape) {
    for (Painted & band : p.bands) release(band);
    p.bands.assign(count, Painted());
    p.print = shape;
  }
  for (size_t i = 0; i < count; i++) {
    int r0 = static_cast<int>(i) * kBandRows, r1 = std::min(r0 + kBandRows, cells.rows());
    Painted & band = p.bands[i];
    uint64_t print = printOf(cells, images, r0, r1, transparent, with_images);
    if (print == band.print && band.valid) continue;
    if (band.texture) g_object_unref(band.texture);
    band.texture = nullptr;
    band.print = print;
    band.valid = true;

    int c0 = 0, c1 = cells.cols();
    if (transparent) {
      c0 = c1;
      c1 = 0;
      for (int r = r0; r < r1; r++) {
        for (int c = 0; c < cells.cols(); c++) {
          if (CellBuffer::isBlank(cells.at(r, c))) continue;
          c0 = std::min(c0, c), c1 = std::max(c1, c + 1);
        }
      }
    }
    if (c0 >= c1) continue;
    auto start = Clock::now();
    Image image = rasterizer->render(cells, with_images ? images : std::vector<ImagePlacement>(), r0, c0, r1 - r0, c1 - c0, cell_h, cell_w, 1,
                                     *display_info.default_fg, *display_info.default_bg);
    if (image.width > 0 && image.height > 0) {
      if (transparent) {
        for (int r = r0; r < r1; r++) {
          for (int c = c0; c < c1; c++) {
            if (!CellBuffer::isBlank(cells.at(r, c))) continue;
            int y_end = std::min(image.height, (r - r0 + 1) * cell_h), x_end = std::min(image.width, (c - c0 + 1) * cell_w);
            for (int y = (r - r0) * cell_h; y < y_end; y++) {
              for (int x = (c - c0) * cell_w; x < x_end; x++) image.pixels[static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)] = 0;
            }
          }
        }
      }
      // The pixels move into the texture's bytes; no copy.
      auto * pixels = new std::vector<uint32_t>(std::move(image.pixels));
      GBytes * bytes = g_bytes_new_with_free_func(
          pixels->data(), pixels->size() * sizeof(uint32_t), [](gpointer data) { delete static_cast<std::vector<uint32_t> *>(data); }, pixels);
      band.texture = gdk_memory_texture_new(image.width, image.height, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED, bytes, static_cast<gsize>(image.width) * 4);
      g_bytes_unref(bytes);
      band.row = r0;
      band.col = c0;
    }
    double ms = msSince(start);
    stats.raster_ms_total += ms;
    stats.raster_ms_max = std::max(stats.raster_ms_max, ms);
  }
  return p;
}

void Window::Impl::appendTexture(GtkSnapshot * snap, const Painted & p, int row, int col) const {
  for (const Painted & band : p.bands) appendTexture(snap, band, row, col);
  if (!p.texture) return;
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(static_cast<float>((p.col + col) * cell_w), static_cast<float>((p.row + row) * cell_h),
                                              static_cast<float>(gdk_texture_get_width(p.texture)), static_cast<float>(gdk_texture_get_height(p.texture)));
  gtk_snapshot_append_scaled_texture(snap, p.texture, GSK_SCALING_FILTER_NEAREST, &bounds);
}

// A soft shadow around every plane above the base that nothing blurs, the
// higher the softer and the further down: drawn over the frame, outside the
// plane, and under every plane higher than it, which is drawn again over it.
void Window::Impl::appendShadows(GtkSnapshot * snap, const Painted & top, GskRenderNode * below) {
  const Color & bg = *display_info.default_bg;
  bool dark = bg.luminance() < 0.21f;
  stats.shadows = 0;
  // Two shadows, as many design systems draw them: a key light from the upper
  // left, offset down by half the height (and right by a little less, so the
  // corner below right is shaded too) and blurred by all of it, and a faint
  // ambient one all around. Height is in cell widths.
  GdkRGBA key = { 0, 0, 0, dark ? 0.65f : 0.18f };
  GdkRGBA ambient = { 0, 0, 0, dark ? 0.4f : 0.12f };
  GdkRGBA highlight = { 1, 1, 1, dark ? 0.14f : 0.55f };
  GdkRGBA edge = { 0, 0, 0, dark ? 0.3f : 0.1f };
  auto rectOf = [&](const ElevatedArea & a) {
    return GRAPHENE_RECT_INIT(static_cast<float>(a.col * cell_w), static_cast<float>(a.row * cell_h), static_cast<float>(a.cols * cell_w),
                              static_cast<float>(a.rows * cell_h));
  };
  auto shown = [&](const ElevatedArea & a) { return a.layer >= layers.size(); };
  // The lifted bands of scrolled planes, which are drawn in the layers below.
  struct LiftedBand {
    graphene_rect_t rect;
    float elevation;
  };
  std::vector<LiftedBand> bands;
  for (const BackdropLayer & layer : layers) {
    if (!below || !layer.scroll || !layer.scroll->band || layer.scroll->band->lift <= 0.0f) continue;
    const ScrollContent & content = *layer.scroll;
    const Band & band = *content.band;
    graphene_rect_t plane = GRAPHENE_RECT_INIT(static_cast<float>(content.col * cell_w), static_cast<float>(content.row * cell_h),
                                               static_cast<float>(content.cols * cell_w), static_cast<float>(content.rows * cell_h));
    graphene_rect_t strip = GRAPHENE_RECT_INIT(static_cast<float>((content.col + band.col) * cell_w), static_cast<float>((content.row + band.row) * cell_h),
                                               static_cast<float>(band.cols * cell_w), static_cast<float>(band.rows * cell_h));
    LiftedBand lifted{ {}, content.elevation + band.lift };
    if (graphene_rect_intersection(&plane, &strip, &lifted.rect)) bands.push_back(lifted);
  }
  // Height by height, lowest first: each one's shadows, then everything as
  // high or higher drawn again over them, so a shadow falls only on what is
  // lower (neighbours of one height included).
  size_t i = 0;
  while (i < elevations.size()) {
    float e = elevations[i].elevation;
    size_t level_end = i;
    while (level_end < elevations.size() && elevations[level_end].elevation == e) level_end++;
    float px = e * static_cast<float>(cell_w);
    float dx = 0.3f * px, dy = 0.5f * px, blur = px, ambient_blur = std::max(2.0f, 0.25f * px);
    graphene_rect_t reach = GRAPHENE_RECT_INIT(0, 0, 0, 0);
    bool any = false;
    for (size_t k = i; k < level_end; k++) {
      if (!shown(elevations[k]) || !elevations[k].shadow) continue;
      graphene_rect_t rect = rectOf(elevations[k]);
      GskRoundedRect outline;
      gsk_rounded_rect_init_from_rect(&outline, &rect, 0);
      gtk_snapshot_append_outset_shadow(snap, &outline, &ambient, 0, 0, 0, ambient_blur);
      gtk_snapshot_append_outset_shadow(snap, &outline, &key, dx, dy, 0, blur);
      stats.shadows++;
      graphene_rect_t r = GRAPHENE_RECT_INIT(rect.origin.x - 2 * blur, rect.origin.y - 2 * ambient_blur, rect.size.width + 4 * blur + dx,
                                             rect.size.height + 2 * ambient_blur + dy + 2 * blur);
      if (any) graphene_rect_union(&reach, &r, &reach);
      else reach = r;
      any = true;
    }
    if (any) {
      // A lifted band again, with what lies below and above it.
      for (const LiftedBand & band : bands) {
        graphene_rect_t meet;
        if (band.elevation < e || !graphene_rect_intersection(&band.rect, &reach, &meet)) continue;
        gtk_snapshot_push_clip(snap, &band.rect);
        gtk_snapshot_append_node(snap, below);
        appendTexture(snap, top);
        gtk_snapshot_pop(snap);
      }
      for (size_t j = i; j < elevations.size(); j++) {
        if (!shown(elevations[j])) continue;
        graphene_rect_t above = rectOf(elevations[j]), meet;
        if (!graphene_rect_intersection(&above, &reach, &meet)) continue;
        gtk_snapshot_push_clip(snap, &above);
        appendTexture(snap, top);
        gtk_snapshot_pop(snap);
      }
      // A light top edge and a darker bottom one, so pieces side by side
      // or stacked read apart.
      for (size_t k = i; k < level_end; k++) {
        if (!shown(elevations[k]) || !elevations[k].shadow) continue;
        graphene_rect_t rect = rectOf(elevations[k]);
        graphene_rect_t top_edge = GRAPHENE_RECT_INIT(rect.origin.x, rect.origin.y, rect.size.width, 1);
        graphene_rect_t bottom_edge = GRAPHENE_RECT_INIT(rect.origin.x, rect.origin.y + rect.size.height - 1, rect.size.width, 1);
        gtk_snapshot_append_color(snap, &highlight, &top_edge);
        gtk_snapshot_append_color(snap, &edge, &bottom_edge);
      }
    }
    i = level_end;
  }
}

void Window::Impl::snapshot(GtkSnapshot * snap) {
  auto start = Clock::now();
  int width = gtk_widget_get_width(view), height = gtk_widget_get_height(view);
  auto channel = [](const Color & c, float alpha = 1.0f) {
    auto rgb = c.toSrgb8();
    return GdkRGBA{ rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, alpha };
  };
  // A band's border, inside its rectangle.
  auto appendBandBorder = [&](GtkSnapshot * target, const Band & band, const ScrollContent & content, double w, double h) {
    if (band.border_px <= 0) return;
    GdkRGBA edge = channel(band.border_color, band.border_alpha);
    double bx = (content.col + band.col) * w, by = (content.row + band.row) * h;
    BandBorderShape shape = bandBorder(band, bx, by, (content.col + band.border_skip_col) * w, w, h);
    for (const BandBorderRect & r : shape.rects) {
      graphene_rect_t rect = GRAPHENE_RECT_INIT(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h));
      gtk_snapshot_append_color(target, &edge, &rect);
    }
    // A round corner is the band's rounded outline cut to the square the arc
    // fills, so the pieces never overlap (a part-transparent border stays even).
    graphene_rect_t whole = GRAPHENE_RECT_INIT(static_cast<float>(bx), static_cast<float>(by), static_cast<float>(band.cols * w), static_cast<float>(band.rows * h));
    float widths[4] = { static_cast<float>(shape.thickness), static_cast<float>(shape.thickness), static_cast<float>(shape.thickness), static_cast<float>(shape.thickness) };
    GdkRGBA colors[4] = { edge, edge, edge, edge };
    for (const BandBorderCorner & corner : shape.corners) {
      graphene_size_t none = GRAPHENE_SIZE_INIT(0, 0), arc = GRAPHENE_SIZE_INIT(static_cast<float>(corner.size), static_cast<float>(corner.size));
      GskRoundedRect outline;
      gsk_rounded_rect_init(&outline, &whole, corner.which == BandCorner::TOP_LEFT ? &arc : &none, corner.which == BandCorner::TOP_RIGHT ? &arc : &none,
                            corner.which == BandCorner::BOTTOM_RIGHT ? &arc : &none, corner.which == BandCorner::BOTTOM_LEFT ? &arc : &none);
      graphene_rect_t square = GRAPHENE_RECT_INIT(static_cast<float>(corner.x), static_cast<float>(corner.y), static_cast<float>(corner.size), static_cast<float>(corner.size));
      gtk_snapshot_push_clip(target, &square);
      gtk_snapshot_append_border(target, &outline, widths, colors);
      gtk_snapshot_pop(target);
    }
  };
  Color fg = *display_info.default_fg, bg = *display_info.default_bg;

  graphene_rect_t all = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(width), static_cast<float>(height));
  GdkRGBA background = channel(bg);
  size_t scrolls = 0;
  if (frame.rows() > 0 && frame.cols() > 0 && rasterizer) {
    if (layers.empty()) {
      gtk_snapshot_append_color(snap, &background, &all);
      const Painted & top = paint(0, frame, false);
      appendTexture(snap, top);
      appendShadows(snap, top, nullptr);
    } else {
      // Each backdrop blurs and dims everything below it, on the GPU: what
      // is composed so far is a node, drawn as it is and again, blurred,
      // inside the backdrop's cells.
      GskRenderNode * below = nullptr;
      for (size_t i = 0; i < layers.size(); i++) {
        const BackdropLayer & layer = layers[i];
        GtkSnapshot * s = gtk_snapshot_new();
        if (below) gtk_snapshot_append_node(s, below);
        else gtk_snapshot_append_color(s, &background, &all);
        appendTexture(s, paint(i, layer.cells, i > 0));
        if (layer.scroll) {
          // A plane scrolled by part of a cell: its own texture, kept while
          // its cells are unchanged, moved by the fraction and cut to the
          // plane. A scroll costs a translation, not a rasterization.
          const ScrollContent & content = *layer.scroll;
          graphene_rect_t shown = GRAPHENE_RECT_INIT(static_cast<float>(content.col * cell_w), static_cast<float>(content.row * cell_h),
                                                     static_cast<float>(content.cols * cell_w), static_cast<float>(content.rows * cell_h));
          graphene_point_t shift = GRAPHENE_POINT_INIT(static_cast<float>(-content.shift_cols * cell_w), static_cast<float>(-content.shift_rows * cell_h));
          if (content.blended()) {
            // A plane laid over what is below with a blend: that as a node, the
            // plane's texture blended with a copy of it inside the plane.
            GskRenderNode * under = gtk_snapshot_free_to_node(s);
            s = gtk_snapshot_new();
            gtk_snapshot_append_node(s, under);
            gtk_snapshot_push_clip(s, &shown);
            gtk_snapshot_push_blend(s, gskBlendMode(content.blend));
            gtk_snapshot_append_node(s, under);
            gtk_snapshot_pop(s);
            gtk_snapshot_push_opacity(s, content.blend_alpha);
            gtk_snapshot_save(s);
            gtk_snapshot_translate(s, &shift);
            appendTexture(s, paint(layers.size() + 1 + scrolls++, content.cells, true, false), content.row, content.col);
            gtk_snapshot_restore(s);
            gtk_snapshot_pop(s);
            gtk_snapshot_pop(s);
            gtk_snapshot_pop(s);
            gsk_render_node_unref(under);
          } else {
          gtk_snapshot_push_clip(s, &shown);
          gtk_snapshot_save(s);
          gtk_snapshot_translate(s, &shift);
          appendTexture(s, paint(layers.size() + 1 + scrolls++, content.cells, true, false), content.row, content.col);
          gtk_snapshot_restore(s);
          if (content.band && content.band->blend == BlendMode::NORMAL) {
            const Band & band = *content.band;
            GdkRGBA tint = channel(band.color, band.alpha);
            graphene_rect_t strip = GRAPHENE_RECT_INIT(static_cast<float>((content.col + band.col) * cell_w), static_cast<float>((content.row + band.row) * cell_h),
                                                       static_cast<float>(band.cols * cell_w), static_cast<float>(band.rows * cell_h));
            gtk_snapshot_append_color(s, &tint, &strip);
            appendBandBorder(s, band, content, cell_w, cell_h);
          }
          gtk_snapshot_pop(s);
          if (content.band && content.band->blend != BlendMode::NORMAL) {
            // A blended band needs what is below it as a node: the strip is drawn
            // blended with a copy of that, inside its own rectangle.
            const Band & band = *content.band;
            GdkRGBA tint = channel(band.color, band.alpha);
            graphene_rect_t strip = GRAPHENE_RECT_INIT(static_cast<float>((content.col + band.col) * cell_w), static_cast<float>((content.row + band.row) * cell_h),
                                                       static_cast<float>(band.cols * cell_w), static_cast<float>(band.rows * cell_h));
            GskRenderNode * under = gtk_snapshot_free_to_node(s);
            s = gtk_snapshot_new();
            gtk_snapshot_append_node(s, under);
            // The blend is drawn at full strength and cross-faded with what is
            // below by the band's alpha: a blend node with a part-transparent
            // top darkens instead of mixing.
            tint.alpha = 1.0f;
            gtk_snapshot_push_clip(s, &strip);
            gtk_snapshot_push_cross_fade(s, static_cast<double>(band.alpha));
            gtk_snapshot_append_node(s, under);
            gtk_snapshot_pop(s);
            gtk_snapshot_push_blend(s, gskBlendMode(band.blend));
            gtk_snapshot_append_node(s, under);
            gtk_snapshot_pop(s);
            gtk_snapshot_append_color(s, &tint, &strip);
            gtk_snapshot_pop(s);
            gtk_snapshot_pop(s);
            appendBandBorder(s, band, content, cell_w, cell_h);
            gtk_snapshot_pop(s);
            gsk_render_node_unref(under);
          }
          }
        }
        GskRenderNode * composed = gtk_snapshot_free_to_node(s);
        if (below) gsk_render_node_unref(below);

        s = gtk_snapshot_new();
        gtk_snapshot_append_node(s, composed);
        graphene_rect_t region = GRAPHENE_RECT_INIT(static_cast<float>(layer.col * cell_w), static_cast<float>(layer.row * cell_h),
                                                    static_cast<float>(layer.cols * cell_w), static_cast<float>(layer.rows * cell_h));
        if (layer.backdrop.blur > 0) {
          // A Gaussian whose spread grows with the backdrop's blur, as two
          // passes of half a row per step would (a blur node's radius is
          // twice its spread).
          double half_row = std::min(layer.backdrop.blur, Backdrop::kMaxBlur) * cell_h / 2.0;
          double radius = 2.0 * std::sqrt(2.0 * half_row * (half_row + 1.0) / 3.0);
          // The edge of the screen blurs with the background, not with nothing.
          float margin = static_cast<float>(3.0 * radius);
          graphene_rect_t backing = GRAPHENE_RECT_INIT(region.origin.x - margin, region.origin.y - margin, region.size.width + 2 * margin,
                                                       region.size.height + 2 * margin);
          gtk_snapshot_push_clip(s, &region);
          gtk_snapshot_push_blur(s, radius);
          gtk_snapshot_append_color(s, &background, &backing);
          gtk_snapshot_append_node(s, composed);
          gtk_snapshot_pop(s);
          gtk_snapshot_pop(s);
        }
        if (layer.backdrop.dim > 0.0f) {
          GdkRGBA tint = channel(layer.backdrop.tint.value_or(bg), layer.backdrop.dim);
          gtk_snapshot_append_color(s, &tint, &region);
        }
        gsk_render_node_unref(composed);
        below = gtk_snapshot_free_to_node(s);
      }
      gtk_snapshot_append_node(snap, below);
      const Painted & top = paint(layers.size(), frame, true);
      appendTexture(snap, top);
      appendShadows(snap, top, below);
      gsk_render_node_unref(below);
    }
    // Layers that are gone leave their textures.
    size_t used = layers.size() + 1 + scrolls;
    for (size_t i = used; i < painted.size(); i++) release(painted[i]);
    painted.resize(used);
  }
  if (cursor.visible) {
    graphene_rect_t bar = GRAPHENE_RECT_INIT(static_cast<float>(cursor.x * cell_w), static_cast<float>(cursor.y * cell_h), 2.0f, static_cast<float>(cell_h));
    GdkRGBA ink = channel(fg);
    gtk_snapshot_append_color(snap, &ink, &bar);
  }
  double ms = msSince(start);
  stats.frames++;
  stats.draw_ms_total += ms;
  stats.draw_ms_max = std::max(stats.draw_ms_max, ms);
}

void Window::Impl::pasteClipboard() {
  GdkClipboard * clipboard = gtk_widget_get_clipboard(view);
  gdk_clipboard_read_text_async(
      clipboard, nullptr,
      [](GObject * source, GAsyncResult * result, gpointer data) {
        auto * self = static_cast<Impl *>(data);
        char * text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, nullptr);
        if (!text) return;
        self->push(BackendEvent::makePaste(text));
        g_free(text);
      },
      this);
}

bool Window::Impl::keyEvent(guint keyval, GdkModifierType state, bool pressed, guint keycode) {
  unsigned mods = modsFrom(state);
  if (pressed && ((keyval == GDK_KEY_Insert && (mods & kModShift)) || ((keyval == GDK_KEY_V || keyval == GDK_KEY_v) && (mods & kModCtrl) && (mods & kModShift)))) {
    pasteClipboard();
    return true;
  }
  int key = keyFor(keyval, mods);
  if (key == 0) return false;
  Input::Kind kind;
  if (pressed) kind = held_keys.insert(keycode).second ? Input::Kind::PRESS : Input::Kind::REPEAT;
  else {
    held_keys.erase(keycode);
    kind = Input::Kind::RELEASE;
  }
  push(BackendEvent::makeInput(keyInput(key, mods, kind)));
  return true;
}

void Window::Impl::buttonEvent(int button, double x, double y, bool pressed, GdkModifierType state) {
  if (button < 1 || button > 3) return;
  int row, col;
  cellAt(x, y, row, col);
  float fy, fx;
  cellPosition(x, y, fy, fx);
  unsigned mods = modsFrom(state);
  held_button = pressed ? button : 0;
  last_row = row;
  last_col = col;
  last_fy = fy;
  last_fx = fx;
  Input input(Key::Button1 + button - 1, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper),
              pressed ? Input::Kind::PRESS : Input::Kind::RELEASE);
  push(BackendEvent::makeInput(input.withPosition(fy, fx)));
}

// Sets the widget's pointer to the shape asked for over the pointer's cell.
void Window::Impl::updateMouseCursor() {
  MouseCursor shape = mouseCursorAt(mouse_cursors, last_row, last_col);
  if (shape == shown_cursor) return;
  shown_cursor = shape;
  gtk_widget_set_cursor_from_name(view, mouseCursorName(shape));
}

void Window::Impl::motionEvent(double x, double y, GdkModifierType state) {
  int row, col;
  cellAt(x, y, row, col);
  float fy, fx;
  cellPosition(x, y, fy, fx);
  if (fy == last_fy && fx == last_fx) return;
  // The pointer's place, for the wheel to be sent to.
  last_row = row;
  last_col = col;
  last_fy = fy;
  last_fx = fx;
  updateMouseCursor();
  unsigned mods = modsFrom(state);
  // A drag repeats the press of the held button at the new position; with
  // no button the pointer is hovering.
  Input input = held_button ? Input(Key::Button1 + held_button - 1, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl,
                                    mods & (kModMeta | kModSuper), Input::Kind::PRESS)
                            : Input(Key::Motion, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper),
                                    Input::Kind::RELEASE);
  push(BackendEvent::makeInput(input.withPosition(fy, fx)));
}

bool Window::Impl::scrollEvent(double dx, double dy, GdkModifierType state) {
  scroll_x += dx;
  scroll_y += dy;
  unsigned mods = modsFrom(state);
  int row = std::max(last_row, 0), col = std::max(last_col, 0);
  auto emit = [&](int button) {
    push(BackendEvent::makeInput(Input(button, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper), Input::Kind::PRESS)
                                     .withPosition(std::max(last_fy, 0.0f), std::max(last_fx, 0.0f))));
  };
  while (scroll_y >= 1.0) { emit(Key::Button5); scroll_y -= 1.0; }
  while (scroll_y <= -1.0) { emit(Key::Button4); scroll_y += 1.0; }
  while (scroll_x >= 1.0) { emit(Key::Button7); scroll_x -= 1.0; }
  while (scroll_x <= -1.0) { emit(Key::Button6); scroll_x += 1.0; }
  return true;
}

std::unique_ptr<Window> Window::open(const WindowOptions & options, std::string * error) {
  if (!gtk_init_check()) {
    if (error) *error = "GTK cannot open a display";
    return nullptr;
  }
  std::unique_ptr<Window> w(new Window());
  Impl & impl = *w->impl_;
  impl.owner = w.get();
  impl.input_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  impl.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

  impl.window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(impl.window), options.title.c_str());
  impl.view = GTK_WIDGET(g_object_new(cellui_view_get_type(), nullptr));
  reinterpret_cast<CelluiView *>(impl.view)->impl = &impl;
  impl.measure(options);
  if (!impl.rasterizer) {
    if (error) *error = "this build draws no pictures (no canvas pixel backend)";
    return nullptr;
  }
  impl.rows = options.rows;
  impl.cols = options.cols;
  impl.frame = CellBuffer(impl.rows, impl.cols);
  gtk_window_set_child(GTK_WINDOW(impl.window), impl.view);
  gtk_window_set_default_size(GTK_WINDOW(impl.window), impl.cols * impl.cell_w, impl.rows * impl.cell_h);

  Impl * self = &impl;
  // The keys are taken in the capture phase: Tab and the arrows would move
  // the focus otherwise.
  GtkEventController * keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey *, guint keyval, guint keycode, GdkModifierType state, gpointer data) -> gboolean {
                     return static_cast<Impl *>(data)->keyEvent(keyval, state, true, keycode);
                   }),
                   self);
  g_signal_connect(keys, "key-released", G_CALLBACK(+[](GtkEventControllerKey *, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
                     static_cast<Impl *>(data)->keyEvent(keyval, state, false, keycode);
                   }),
                   self);
  gtk_widget_add_controller(impl.window, keys);

  GtkGesture * click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
  g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick * gesture, gint, gdouble x, gdouble y, gpointer data) {
                     auto * g = GTK_GESTURE_SINGLE(gesture);
                     auto state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
                     static_cast<Impl *>(data)->buttonEvent(static_cast<int>(gtk_gesture_single_get_current_button(g)), x, y, true, state);
                   }),
                   self);
  g_signal_connect(click, "released", G_CALLBACK(+[](GtkGestureClick * gesture, gint, gdouble x, gdouble y, gpointer data) {
                     auto * g = GTK_GESTURE_SINGLE(gesture);
                     auto state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
                     static_cast<Impl *>(data)->buttonEvent(static_cast<int>(gtk_gesture_single_get_current_button(g)), x, y, false, state);
                   }),
                   self);
  gtk_widget_add_controller(impl.view, GTK_EVENT_CONTROLLER(click));

  GtkEventController * motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(+[](GtkEventControllerMotion * controller, gdouble x, gdouble y, gpointer data) {
                     auto state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
                     static_cast<Impl *>(data)->motionEvent(x, y, state);
                   }),
                   self);
  gtk_widget_add_controller(impl.view, motion);

  GtkEventController * scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll * controller, gdouble dx, gdouble dy, gpointer data) -> gboolean {
                     auto state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
                     return static_cast<Impl *>(data)->scrollEvent(dx, dy, state);
                   }),
                   self);
  gtk_widget_add_controller(impl.view, scroll);

  g_signal_connect(impl.window, "notify::is-active", G_CALLBACK(+[](GObject * window, GParamSpec *, gpointer data) {
                     static_cast<Impl *>(data)->push(BackendEvent::makeFocus(gtk_window_is_active(GTK_WINDOW(window))));
                   }),
                   self);
  g_signal_connect(impl.window, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer data) -> gboolean {
                     static_cast<Impl *>(data)->push(BackendEvent::makeClose());
                     return TRUE;  // the application ends the window by ending
                   }),
                   self);

  gtk_window_present(GTK_WINDOW(impl.window));
  gtk_widget_grab_focus(impl.view);
  return w;
}

Window::Window() : impl_(new Impl) { }

Window::~Window() {
  if (!impl_) return;
  if (impl_->view) reinterpret_cast<CelluiView *>(impl_->view)->impl = nullptr;
  if (impl_->window) gtk_window_destroy(GTK_WINDOW(impl_->window));
  impl_->forgetTextures();
  if (impl_->input_fd >= 0) ::close(impl_->input_fd);
  if (impl_->wake_fd >= 0) ::close(impl_->wake_fd);
}

const DisplayInfo & Window::display() const { return impl_->display_info; }
int Window::rows() const { return impl_->rows; }
int Window::cols() const { return impl_->cols; }
EventLoop & Window::eventLoop() { return impl_->loop; }
int Window::inputFd() const { return impl_->input_fd; }
int Window::wakeFd() const { return impl_->wake_fd; }
const FrameStats & Window::stats() const { return impl_->stats; }

std::vector<BackendEvent> Window::poll() {
  impl_->drain(impl_->input_fd);
  impl_->drain(impl_->wake_fd);
  std::vector<BackendEvent> out(impl_->queue.begin(), impl_->queue.end());
  impl_->queue.clear();
  return out;
}

void Window::present(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images, const FrameInfo * info) {
  impl_->frame = frame;
  impl_->cursor = cursor;
  impl_->images = images;
  impl_->layers = info ? info->backdrops : std::vector<BackdropLayer>();
  impl_->elevations = info ? info->elevations : std::vector<ElevatedArea>();
  std::stable_sort(impl_->elevations.begin(), impl_->elevations.end(),
                   [](const ElevatedArea & a, const ElevatedArea & b) { return a.elevation < b.elevation; });
  if (info) {
    impl_->mouse_cursors = info->mouse_cursors;
    impl_->updateMouseCursor();
  }
  gtk_widget_queue_draw(impl_->view);
}

void Window::redraw() {
  impl_->forgetTextures();
  gtk_widget_queue_draw(impl_->view);
}

void Window::wake() { impl_->signal(impl_->wake_fd); }

}  // namespace cellui::gtk
