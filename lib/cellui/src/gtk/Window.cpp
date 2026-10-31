#include <cellui/gtk/Window.h>

#include <cellui/CellRasterizer.h>
#include <cellui/Utf8.h>
#include <cellui/gtk/GlibEventLoop.h>

#include <gtk/gtk.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <tuple>

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

constexpr int kBandRows = 4;

struct Window::Impl {
  Window * owner = nullptr;
  GlibEventLoop loop;
  // Frame pacing: the handler runs in a tick callback, which is on the view
  // only while a frame is wanted.
  FrameHandler frame_handler;
  bool frame_dirty = false;
  // Ticks since one asked for another; the tick callback goes on for a few so
  // that a stream of requests (the scopes' results, between two ticks) finds
  // it still there, instead of adding it again each frame and paying the frame
  // clock's start-up for every one.
  int idle_ticks = 0;
  static constexpr int kLingerTicks = 8;
  bool in_tick = false;
  bool presented_in_tick = false;
  guint tick_id = 0;
  static gboolean onTick(GtkWidget *, GdkFrameClock * clock, gpointer data);
  void noteTick(gint64 frame_time_us, bool continuing);
  // The link: ticks that follow a tick whose handler asked for another one are the window running
  // flat out, and the intervals between them are what it can do. Ticks after an idle gap say nothing.
  LinkMonitor link;
  bool tick_continuing = false;
  gint64 last_tick_us = 0;
  std::deque<double> tick_intervals;  // seconds
  std::chrono::steady_clock::time_point link_last_update{};
  std::string vsyncText() const;
  GtkWidget * window = nullptr;
  GtkWidget * view = nullptr;
  std::unique_ptr<CellRasterizer> rasterizer;
  // The cell font in its four weights and slants (by bold | italic << 1),
  // and one render node per glyph, colour and face, shared by every cell
  // that shows it, so a glyph is shaped once.
  PangoContext * text_context = nullptr;
  PangoFontDescription * cell_fonts[4] = {};
  struct Glyph {
    std::string text;
    uint32_t color;
    int style;
    bool operator<(const Glyph & o) const { return std::tie(text, color, style) < std::tie(o.text, o.color, o.style); }
  };
  std::map<Glyph, GskRenderNode *> glyph_nodes;
  GskRenderNode * glyphNode(const CellBuffer::Cell & cell, const Color & fg);
  GskRenderNode * textNode(const CellBuffer & cells, int r0, int r1);

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
  // The runs of text the display lays out, which cells are marked with.
  std::vector<TextRun> text_runs;
  // The proportional face: the system's own UI font, sized to a cell's height.
  PangoFontDescription * proportional = nullptr;
  // The areas planes asked a pointer shape for, and the one now set.
  std::vector<CursorArea> mouse_cursors;
  MouseCursor shown_cursor = MouseCursor::DEFAULT;
  // What a layer's cells are drawn from, kept while the cells it comes from
  // are unchanged, in bands of rows so a change in a few rows (the scopes,
  // while playing) redoes only those: the backgrounds as a render node of
  // colour rectangles, and the text as a see-through texture. Pictures are not
  // here: they are textures of their own (imageTexture()), laid over the
  // backgrounds each frame.
  struct Painted {
    struct Band {
      uint64_t bg_print = 0, text_print = 0;
      bool bg_valid = false, text_valid = false;
      GskRenderNode * bg = nullptr;
      GskRenderNode * text = nullptr;  // glyphs and underlines, from the band's top-left corner
    };
    uint64_t shape = 0;
    std::vector<Band> bands;
  };
  // The textures of the pictures cells show, by the picture they come from:
  // a picture is uploaded when it is new or has a new version.
  struct ImageTexture {
    std::shared_ptr<const Image> image;
    uint64_t version = 0;
    GdkTexture * texture = nullptr;
    bool used = false;
  };
  std::map<const Image *, ImageTexture> image_textures;
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
  const Painted & paint(size_t slot, const CellBuffer & cells, bool transparent);
  // A layer: backgrounds, then the pictures its cells show (`cells`, null for
  // none), then the text; at cell (row, col) of the screen.
  void appendPainted(GtkSnapshot * snapshot, const Painted & painted, const CellBuffer * cells, int row = 0, int col = 0);
  void appendImages(GtkSnapshot * snapshot, const CellBuffer & cells);
  void appendTextRuns(GtkSnapshot * snapshot, const CellBuffer & cells);
  GdkTexture * imageTexture(const ImagePlacement & placement);
  static void release(Painted & p) {
    for (Painted::Band & band : p.bands) releaseBand(band);
    p = {};
  }
  static void releaseBand(Painted::Band & band) {
    if (band.bg) gsk_render_node_unref(band.bg);
    if (band.text) gsk_render_node_unref(band.text);
    band = {};
  }
  void appendShadows(GtkSnapshot * snap, const Painted & top, GskRenderNode * below, const std::vector<GskRenderNode *> & stacks);
  void forgetTextures() {
    for (Painted & p : painted) release(p);
    for (auto & [key, entry] : image_textures) {
      if (entry.texture) g_object_unref(entry.texture);
    }
    image_textures.clear();
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

std::string Window::Impl::vsyncText() const {
  GdkMonitor * monitor = nullptr;
  if (window) {
    if (GdkSurface * surface = gtk_native_get_surface(GTK_NATIVE(window))) monitor = gdk_display_get_monitor_at_surface(gtk_widget_get_display(window), surface);
  }
  int milli_hz = monitor ? gdk_monitor_get_refresh_rate(monitor) : 0;
  if (milli_hz <= 0) return "vsync unknown";
  double hz = milli_hz / 1000.0;
  char buf[32];
  if (std::fabs(hz - std::round(hz)) < 0.05) std::snprintf(buf, sizeof buf, "%d Hz vsync", static_cast<int>(std::lround(hz)));
  else std::snprintf(buf, sizeof buf, "%.2f Hz vsync", hz);
  return buf;
}

void Window::Impl::noteTick(gint64 frame_time_us, bool continuing) {
  double since_last = last_tick_us > 0 ? static_cast<double>(frame_time_us - last_tick_us) / 1e6 : 0.0;
  // A gap is a restart, not a slow frame.
  if (tick_continuing && last_tick_us > 0 && since_last < 0.2) {
    double interval = since_last;
    if (interval > 0.025) stats.late_ticks++;
    tick_intervals.push_back(interval);
    while (tick_intervals.size() > 60) tick_intervals.pop_front();
  } else {
    tick_intervals.clear();
  }
  tick_continuing = continuing;
  last_tick_us = frame_time_us;
  auto now = std::chrono::steady_clock::now();
  if (tick_intervals.size() < 8 || now - link_last_update < std::chrono::seconds(1)) return;
  link_last_update = now;
  double sum = 0;
  for (double v : tick_intervals) sum += v;
  double fps = sum > 0 ? static_cast<double>(tick_intervals.size()) / sum : 0;
  link.setDescription(vsyncText() + ", " + std::to_string(static_cast<int>(fps + 0.5)) + " fps");
  link.update(now, linkClassForFps(fps), linkClassForFps(fps, link.classifierConfig().up_margin));
}

gboolean Window::Impl::onTick(GtkWidget *, GdkFrameClock * clock, gpointer data) {
  auto * self = static_cast<Impl *>(data);
  // A tick that nobody asked for (one of the few that linger) runs nothing.
  bool wanted = self->frame_dirty;
  self->frame_dirty = false;
  self->in_tick = true;
  self->presented_in_tick = false;
  if (wanted) {
    self->stats.ticks++;
    if (self->frame_handler) self->frame_handler(gdk_frame_clock_get_frame_time(clock));
  }
  self->in_tick = false;
  bool keep = wanted || self->frame_dirty || ++self->idle_ticks <= kLingerTicks;
  if (wanted || self->frame_dirty) self->idle_ticks = 0;
  self->noteTick(gdk_frame_clock_get_frame_time(clock), wanted);
  // Only a frame the handler presented is drawn; a tick that found nothing
  // new draws nothing.
  if (self->presented_in_tick) gtk_widget_queue_draw(self->view);
  if (keep) return G_SOURCE_CONTINUE;
  self->tick_id = 0;
  return G_SOURCE_REMOVE;
}

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
  text_context = gtk_widget_create_pango_context(view);
  for (int i = 0; i < 4; i++) {
    cell_fonts[i] = pango_font_description_copy(desc);
    if (i & 1) pango_font_description_set_weight(cell_fonts[i], PANGO_WEIGHT_BOLD);
    if (i & 2) pango_font_description_set_style(cell_fonts[i], PANGO_STYLE_ITALIC);
  }
  g_object_unref(layout);
  g_object_unref(context);
  pango_font_description_free(desc);

  // The proportional face, sized so that a line of it fits a cell's height.
  {
    PangoContext * ui = gtk_widget_get_pango_context(view);
    proportional = pango_font_description_copy(pango_context_get_font_description(ui));
    PangoLayout * probe = pango_layout_new(ui);
    pango_layout_set_text(probe, "Mgjy", -1);
    double px = cell_h * 0.72;
    for (int i = 0; i < 12; i++) {
      pango_font_description_set_absolute_size(proportional, px * PANGO_SCALE);
      pango_layout_set_font_description(probe, proportional);
      int w = 0, h = 0;
      pango_layout_get_pixel_size(probe, &w, &h);
      if (h <= cell_h) break;
      px *= 0.92;
    }
    g_object_unref(probe);
  }

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
  display_info.smooth_scroll = true;
  display_info.proportional_text = true;
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

// What a cell's background is drawn as: none (see-through or the window's
// own), else a colour. A picture, and the second half of a wide character, sit
// on the window's background.
std::optional<Color> backgroundOf(const CellBuffer::Cell & cell, bool transparent, const Color & window_bg) {
  if (transparent && CellBuffer::isBlank(cell)) return std::nullopt;
  if (cell.image > 0 || cell.continuation) return window_bg;
  return cell.bg.value_or(window_bg);
}

// Whether a cell has anything to draw as text: a glyph or an underline, and is
// not under a picture or the second half of a wide character.
bool hasText(const CellBuffer::Cell & cell) {
  if (cell.image > 0 || cell.continuation) return false;
  return cell.underline || (!cell.text.empty() && cell.text != " ");
}

}  // namespace

// The backgrounds of rows [r0, r1) as colour rectangles, one for each run of
// cells of one colour. In the opaque layer the window's own background is
// already drawn, so cells of that colour need none.
static GskRenderNode * backgroundNode(const CellBuffer & cells, int r0, int r1, bool transparent, const Color & window_bg, int cell_w, int cell_h) {
  GtkSnapshot * s = gtk_snapshot_new();
  auto window_rgb = window_bg.toSrgb8();
  for (int r = r0; r < r1; r++) {
    for (int c = 0; c < cells.cols();) {
      auto color = backgroundOf(cells.at(r, c), transparent, window_bg);
      int end = c + 1;
      if (color) {
        auto rgb = color->toSrgb8();
        while (end < cells.cols()) {
          auto next = backgroundOf(cells.at(r, end), transparent, window_bg);
          if (!next || next->toSrgb8() != rgb) break;
          end++;
        }
        if (transparent || rgb != window_rgb) {
          GdkRGBA rgba = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f };
          graphene_rect_t rect = GRAPHENE_RECT_INIT(static_cast<float>(c * cell_w), static_cast<float>(r * cell_h), static_cast<float>((end - c) * cell_w),
                                                    static_cast<float>(cell_h));
          gtk_snapshot_append_color(s, &rgba, &rect);
        }
      }
      c = end;
    }
  }
  return gtk_snapshot_free_to_node(s);
}

// A layer's cells as bands of rows, each redone only when what it is made of
// changed: its backgrounds when a cell's colour did, its text when a glyph,
// colour or style did. A picture changing redoes neither.
const Window::Impl::Painted & Window::Impl::paint(size_t slot, const CellBuffer & cells, bool transparent) {
  if (painted.size() <= slot) painted.resize(slot + 1);
  Painted & p = painted[slot];
  size_t count = static_cast<size_t>((cells.rows() + kBandRows - 1) / kBandRows);
  uint64_t shape = static_cast<uint64_t>(cells.rows()) << 32 | static_cast<uint32_t>(cells.cols());
  if (p.bands.size() != count || p.shape != shape) {
    for (Painted::Band & band : p.bands) releaseBand(band);
    p.bands.assign(count, Painted::Band());
    p.shape = shape;
  }
  const Color & window_bg = *display_info.default_bg;
  for (size_t i = 0; i < count; i++) {
    int r0 = static_cast<int>(i) * kBandRows, r1 = std::min(r0 + kBandRows, cells.rows());
    Painted::Band & band = p.bands[i];

    Print bg_print, text_print;
    bg_print.add(static_cast<uint64_t>(r0) << 32 | static_cast<uint32_t>(cells.cols()));
    bg_print.add(transparent);
    text_print.add(static_cast<uint64_t>(r0) << 32 | static_cast<uint32_t>(cells.cols()));
    for (int r = r0; r < r1; r++) {
      for (int c = 0; c < cells.cols(); c++) {
        const CellBuffer::Cell & cell = cells.at(r, c);
        bg_print.add(backgroundOf(cell, transparent, window_bg));
        if (!hasText(cell)) {
          text_print.add(static_cast<uint64_t>(0));
          continue;
        }
        text_print.add(cell.text);
        text_print.add(cell.fg);
        text_print.add(static_cast<uint64_t>(cell.bold) | static_cast<uint64_t>(cell.italic) << 1 | static_cast<uint64_t>(cell.underline) << 2);
      }
    }

    if (!band.bg_valid || band.bg_print != bg_print.value()) {
      if (band.bg) gsk_render_node_unref(band.bg);
      band.bg = backgroundNode(cells, r0, r1, transparent, window_bg, cell_w, cell_h);
      band.bg_print = bg_print.value();
      band.bg_valid = true;
    }

    if (!band.text_valid || band.text_print != text_print.value()) {
      if (band.text) gsk_render_node_unref(band.text);
      band.text_print = text_print.value();
      band.text_valid = true;
      auto start = Clock::now();
      band.text = textNode(cells, r0, r1);
      double ms = msSince(start);
      stats.raster_ms_total += ms;
      stats.raster_ms_max = std::max(stats.raster_ms_max, ms);
    }
  }
  return p;
}

// One glyph in a cell's size, at the cell's corner: shaped by Pango, which
// finds a font for a character the cell font lacks; one that comes out wider
// than its cells is shrunk to them.
GskRenderNode * Window::Impl::glyphNode(const CellBuffer::Cell & cell, const Color & fg) {
  auto rgb = fg.toSrgb8();
  Glyph key{ cell.text, static_cast<uint32_t>(rgb[0]) << 16 | static_cast<uint32_t>(rgb[1]) << 8 | rgb[2],
             (cell.bold ? 1 : 0) | (cell.italic ? 2 : 0) };
  auto found = glyph_nodes.find(key);
  if (found != glyph_nodes.end()) return found->second;
  if (glyph_nodes.size() > 4096) {
    for (auto & entry : glyph_nodes) {
      if (entry.second) gsk_render_node_unref(entry.second);
    }
    glyph_nodes.clear();
  }
  PangoLayout * layout = pango_layout_new(text_context);
  pango_layout_set_font_description(layout, cell_fonts[key.style]);
  pango_layout_set_text(layout, cell.text.c_str(), -1);
  int w = 0, h = 0;
  pango_layout_get_pixel_size(layout, &w, &h);
  int wide = std::max(1, utf8::displayWidth(cell.text));
  GtkSnapshot * snap = gtk_snapshot_new();
  float scale = w > wide * cell_w ? static_cast<float>(wide * cell_w) / static_cast<float>(w) : 1.0f;
  float top = std::max(0.0f, (static_cast<float>(cell_h) - static_cast<float>(h) * scale) / 2.0f);
  graphene_point_t at = GRAPHENE_POINT_INIT(0.0f, top);
  gtk_snapshot_translate(snap, &at);
  if (scale < 1.0f) gtk_snapshot_scale(snap, scale, scale);
  GdkRGBA color = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f };
  gtk_snapshot_append_layout(snap, layout, &color);
  GskRenderNode * node = gtk_snapshot_free_to_node(snap);
  g_object_unref(layout);
  glyph_nodes[key] = node;
  return node;
}

// The glyphs and underlines of rows [r0, r1), a cell at a time so each sits
// on the grid whatever the font's advances are.
GskRenderNode * Window::Impl::textNode(const CellBuffer & cells, int r0, int r1) {
  GtkSnapshot * snap = gtk_snapshot_new();
  const Color & default_fg = *display_info.default_fg;
  bool any = false;
  for (int r = r0; r < r1; r++) {
    for (int c = 0; c < cells.cols(); c++) {
      const CellBuffer::Cell & cell = cells.at(r, c);
      if (!hasText(cell)) continue;
      any = true;
      Color fg = cell.fg.value_or(default_fg);
      gtk_snapshot_save(snap);
      graphene_point_t at = GRAPHENE_POINT_INIT(static_cast<float>(c * cell_w), static_cast<float>((r - r0) * cell_h));
      gtk_snapshot_translate(snap, &at);
      if (!cell.text.empty() && cell.text != " ") {
        if (GskRenderNode * glyph = glyphNode(cell, fg)) gtk_snapshot_append_node(snap, glyph);
      }
      if (cell.underline) {
        auto rgb = fg.toSrgb8();
        GdkRGBA color = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f };
        float thickness = std::max(1.0f, static_cast<float>(cell_h) / 14.0f);
        int wide = cell.text.empty() ? 1 : std::max(1, utf8::displayWidth(cell.text));
        graphene_rect_t line = GRAPHENE_RECT_INIT(0.0f, static_cast<float>(cell_h) - 2.0f * thickness, static_cast<float>(wide * cell_w), thickness);
        gtk_snapshot_append_color(snap, &color, &line);
      }
      gtk_snapshot_restore(snap);
    }
  }
  GskRenderNode * node = gtk_snapshot_free_to_node(snap);
  if (!any && node) {
    gsk_render_node_unref(node);
    return nullptr;
  }
  return node;
}

// The texture of a picture, uploaded when it is new or has changed.
GdkTexture * Window::Impl::imageTexture(const ImagePlacement & placement) {
  if (!placement.image || placement.image->width <= 0 || placement.image->height <= 0) return nullptr;
  ImageTexture & entry = image_textures[placement.image.get()];
  entry.used = true;
  if (!entry.texture || entry.version != placement.image->version || entry.image != placement.image) {
    if (entry.texture) g_object_unref(entry.texture);
    const Image & image = *placement.image;
    GBytes * bytes = g_bytes_new(image.pixels.data(), image.pixels.size() * sizeof(uint32_t));
    entry.texture = gdk_memory_texture_new(image.width, image.height, GDK_MEMORY_R8G8B8A8, bytes, static_cast<gsize>(image.width) * 4);
    g_bytes_unref(bytes);
    entry.image = placement.image;
    entry.version = image.version;
  }
  return entry.texture;
}

// The rectangles of cells marked by `mark` (an image's or a text run's index +
// 1), by mark: runs along a row, the same run right below one growing it.
namespace {
struct MarkedRun {
  int row, col, rows, cols;
};
template <typename Mark>
std::map<int, std::vector<MarkedRun>> markedRuns(const CellBuffer & cells, Mark mark) {
  std::map<int, std::vector<MarkedRun>> runs;
  for (int r = 0; r < cells.rows(); r++) {
    for (int c = 0; c < cells.cols();) {
      int index = mark(cells.at(r, c));
      if (index <= 0) {
        c++;
        continue;
      }
      int end = c + 1;
      while (end < cells.cols() && mark(cells.at(r, end)) == index) end++;
      auto & list = runs[index];
      auto above = std::find_if(list.begin(), list.end(), [&](const MarkedRun & run) { return run.col == c && run.cols == end - c && run.row + run.rows == r; });
      if (above != list.end()) above->rows++;
      else list.push_back(MarkedRun{ r, c, 1, end - c });
      c = end;
    }
  }
  return runs;
}
}  // namespace

// The pictures the cells show, each over the cells marked with it: the whole
// texture at its place, cut to those cells.
void Window::Impl::appendImages(GtkSnapshot * snap, const CellBuffer & cells) {
  auto runs = markedRuns(cells, [](const CellBuffer::Cell & cell) { return cell.image; });
  for (const auto & [index, list] : runs) {
    if (static_cast<size_t>(index) > images.size()) continue;
    const ImagePlacement & placement = images[static_cast<size_t>(index - 1)];
    GdkTexture * texture = imageTexture(placement);
    if (!texture) continue;
    const Image & image = *placement.image;
    // A picture stretched over cells shows its middle, the edge there only to
    // scale with.
    float x = static_cast<float>(placement.col * cell_w), y = static_cast<float>(placement.row * cell_h);
    float w = static_cast<float>(image.width), h = static_cast<float>(image.height);
    GskScalingFilter filter = GSK_SCALING_FILTER_NEAREST;
    if (image.stretched()) {
      float inner_w = static_cast<float>(std::max(1, image.width - 2 * image.pad)), inner_h = static_cast<float>(std::max(1, image.height - 2 * image.pad));
      float sx = static_cast<float>(image.cols * cell_w) / inner_w, sy = static_cast<float>(image.rows * cell_h) / inner_h;
      x -= static_cast<float>(image.pad) * sx;
      y -= static_cast<float>(image.pad) * sy;
      w *= sx;
      h *= sy;
      filter = GSK_SCALING_FILTER_LINEAR;
    }
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(x, y, w, h);
    for (const MarkedRun & run : list) {
      graphene_rect_t clip = GRAPHENE_RECT_INIT(static_cast<float>(run.col * cell_w), static_cast<float>(run.row * cell_h),
                                                static_cast<float>(run.cols * cell_w), static_cast<float>(run.rows * cell_h));
      gtk_snapshot_push_clip(snap, &clip);
      gtk_snapshot_append_scaled_texture(snap, texture, filter, &bounds);
      gtk_snapshot_pop(snap);
    }
  }
}

// The text the display lays out, each run over the cells marked with it: set
// in the proportional face, centred in the rows, cut to the box (with an
// ellipsis when asked, by the layout itself).
void Window::Impl::appendTextRuns(GtkSnapshot * snap, const CellBuffer & cells) {
  if (text_runs.empty() || !proportional) return;
  auto runs = markedRuns(cells, [](const CellBuffer::Cell & cell) { return cell.text_run; });
  for (const auto & [index, list] : runs) {
    if (static_cast<size_t>(index) > text_runs.size()) continue;
    const TextRun & run = text_runs[static_cast<size_t>(index - 1)];
    auto rgb = run.fg.value_or(*display_info.default_fg).toSrgb8();
    GdkRGBA color = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f };
    PangoFontDescription * font = pango_font_description_copy(proportional);
    if (run.bold) pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
    if (run.italic) pango_font_description_set_style(font, PANGO_STYLE_ITALIC);
    for (const MarkedRun & box : list) {
      float x = static_cast<float>(box.col * cell_w), y = static_cast<float>(box.row * cell_h);
      float w = static_cast<float>(box.cols * cell_w), h = static_cast<float>(box.rows * cell_h);
      PangoLayout * layout = gtk_widget_create_pango_layout(view, run.text.c_str());
      pango_layout_set_font_description(layout, font);
      pango_layout_set_single_paragraph_mode(layout, TRUE);
      if (run.overflow == TextOverflow::ELLIPSIS) {
        pango_layout_set_width(layout, static_cast<int>(w) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
      }
      if (run.underline) {
        PangoAttrList * attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
      }
      int text_w = 0, text_h = 0;
      pango_layout_get_pixel_size(layout, &text_w, &text_h);
      graphene_rect_t clip = GRAPHENE_RECT_INIT(x, y, w, h);
      graphene_point_t at = GRAPHENE_POINT_INIT(x, y + std::max(0.0f, (h - static_cast<float>(text_h)) / 2.0f));
      gtk_snapshot_push_clip(snap, &clip);
      gtk_snapshot_save(snap);
      gtk_snapshot_translate(snap, &at);
      gtk_snapshot_append_layout(snap, layout, &color);
      gtk_snapshot_restore(snap);
      gtk_snapshot_pop(snap);
      g_object_unref(layout);
    }
    pango_font_description_free(font);
  }
}

void Window::Impl::appendPainted(GtkSnapshot * snap, const Painted & p, const CellBuffer * cells, int row, int col) {
  bool shifted = row != 0 || col != 0;
  if (shifted) {
    graphene_point_t at = GRAPHENE_POINT_INIT(static_cast<float>(col * cell_w), static_cast<float>(row * cell_h));
    gtk_snapshot_save(snap);
    gtk_snapshot_translate(snap, &at);
  }
  for (const Painted::Band & band : p.bands) {
    if (band.bg) gtk_snapshot_append_node(snap, band.bg);
  }
  if (cells) appendImages(snap, *cells);
  for (size_t i = 0; i < p.bands.size(); i++) {
    const Painted::Band & band = p.bands[i];
    if (!band.text) continue;
    graphene_point_t top = GRAPHENE_POINT_INIT(0.0f, static_cast<float>(static_cast<int>(i) * kBandRows * cell_h));
    gtk_snapshot_save(snap);
    gtk_snapshot_translate(snap, &top);
    gtk_snapshot_append_node(snap, band.text);
    gtk_snapshot_restore(snap);
  }
  if (cells) appendTextRuns(snap, *cells);
  if (shifted) gtk_snapshot_restore(snap);
}

// A soft shadow around every plane above the base that nothing blurs, the
// higher the softer and the further down: drawn over the frame, outside the
// plane, and under every plane higher than it, which is drawn again over it.
void Window::Impl::appendShadows(GtkSnapshot * snap, const Painted & top, GskRenderNode * below, const std::vector<GskRenderNode *> & stacks) {
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
    return GRAPHENE_RECT_INIT(static_cast<float>((a.col - a.shift_col) * cell_w), static_cast<float>((a.row - a.shift_row) * cell_h), static_cast<float>(a.cols * cell_w),
                              static_cast<float>(a.rows * cell_h));
  };
  // An area's corner radius in pixels: its logical one by the display's scale,
  // at most half the shorter side.
  auto radiusOf = [&](const ElevatedArea & a) {
    float limit = 0.5f * static_cast<float>(std::min(a.cols * cell_w, a.rows * cell_h));
    return std::clamp(static_cast<float>(a.radius * display_info.display_scale), 0.0f, limit);
  };
  // Only a backdrop at or above an area's layer covers it (a plane scrolling
  // by part of a cell is a layer too, and covers nothing).
  std::vector<bool> covered(layers.size() + 1, false);
  for (size_t j = layers.size(); j-- > 0;) covered[j] = covered[j + 1] || layers[j].backdrop.active();
  auto shown = [&](const ElevatedArea & a) { return !covered[std::min(a.layer, layers.size())]; };
  // An area's cells again: from the frame, or, when they were composed into a
  // layer below it, from the stack so far with the frame over it.
  auto redraw = [&](const ElevatedArea & a) {
    if (a.layer < layers.size() && below) gtk_snapshot_append_node(snap, below);
    appendPainted(snap, top, &frame);
  };
  // What shows in the corners a radius cuts away: what lay under them (the
  // stack below, or the cell's colour) with the area's shadow over it. Drawn
  // after the cells of every area of its height, which cover the corners.
  auto fillCorners = [&](const ElevatedArea & a, const GskRoundedRect & outline, float dx, float dy, float blur, float ambient_blur) {
    float r = radiusOf(a);
    if (r <= 0.0f) return;
    graphene_rect_t rect = rectOf(a);
    for (int k = 0; k < 4; k++) {
      graphene_rect_t ear = GRAPHENE_RECT_INIT(k % 2 ? rect.origin.x + rect.size.width - r : rect.origin.x,
                                               k / 2 ? rect.origin.y + rect.size.height - r : rect.origin.y, r, r);
      const auto & under = a.corner_bg[static_cast<size_t>(k)];
      gtk_snapshot_push_clip(snap, &ear);
      // Without an opaque cell under it, the stack as it was before the area's
      // own cells were composed (never the final one, which holds them).
      if (a.layer > 0 && a.layer <= stacks.size()) {
        gtk_snapshot_append_node(snap, stacks[a.layer - 1]);
      } else {
        const Color & c = under ? *under : bg;
        auto rgb = c.toSrgb8();
        GdkRGBA fill = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f };
        gtk_snapshot_append_color(snap, &fill, &ear);
      }
      gtk_snapshot_append_outset_shadow(snap, &outline, &ambient, 0, 0, 0, ambient_blur);
      gtk_snapshot_append_outset_shadow(snap, &outline, &key, dx, dy, 0, blur);
      gtk_snapshot_pop(snap);
    }
  };
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
      gsk_rounded_rect_init_from_rect(&outline, &rect, radiusOf(elevations[k]));
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
        appendPainted(snap, top, &frame);
        gtk_snapshot_pop(snap);
      }
      for (size_t j = i; j < elevations.size(); j++) {
        if (!shown(elevations[j]) || elevations[j].scrim) continue;
        graphene_rect_t above = rectOf(elevations[j]), meet;
        if (!graphene_rect_intersection(&above, &reach, &meet)) continue;
        GskRoundedRect shape;
        gsk_rounded_rect_init_from_rect(&shape, &above, radiusOf(elevations[j]));
        gtk_snapshot_push_rounded_clip(snap, &shape);
        redraw(elevations[j]);
        gtk_snapshot_pop(snap);
      }
      for (size_t k = i; k < level_end; k++) {
        if (!shown(elevations[k]) || !elevations[k].shadow || radiusOf(elevations[k]) <= 0.0f) continue;
        graphene_rect_t rect = rectOf(elevations[k]);
        GskRoundedRect outline;
        gsk_rounded_rect_init_from_rect(&outline, &rect, radiusOf(elevations[k]));
        fillCorners(elevations[k], outline, dx, dy, blur, ambient_blur);
        // The ears are squares; the area's own rounded body goes back over them.
        gtk_snapshot_push_rounded_clip(snap, &outline);
        redraw(elevations[k]);
        gtk_snapshot_pop(snap);
      }
      // A light top edge and a darker bottom one, so pieces side by side
      // or stacked read apart; a border that thins to nothing at the sides
      // follows a rounded corner.
      for (size_t k = i; k < level_end; k++) {
        if (!shown(elevations[k]) || !elevations[k].shadow) continue;
        graphene_rect_t rect = rectOf(elevations[k]);
        GskRoundedRect outline;
        gsk_rounded_rect_init_from_rect(&outline, &rect, radiusOf(elevations[k]));
        float widths[4] = { 1, 0, 1, 0 };
        GdkRGBA colors[4] = { highlight, highlight, edge, edge };
        gtk_snapshot_append_border(snap, &outline, widths, colors);
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
      appendPainted(snap, top, &frame);
      appendShadows(snap, top, nullptr, {});
    } else {
      // Each backdrop blurs and dims everything below it, on the GPU: what
      // is composed so far is a node, drawn as it is and again, blurred,
      // inside the backdrop's cells.
      GskRenderNode * below = nullptr;
      std::vector<GskRenderNode *> stacks;
      for (size_t i = 0; i < layers.size(); i++) {
        const BackdropLayer & layer = layers[i];
        GtkSnapshot * s = gtk_snapshot_new();
        if (below) gtk_snapshot_append_node(s, below);
        else gtk_snapshot_append_color(s, &background, &all);
        appendPainted(s, paint(i, layer.cells, i > 0), &layer.cells);
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
            appendPainted(s, paint(layers.size() + 1 + scrolls++, content.cells, true), &content.cells, content.row, content.col);
            gtk_snapshot_restore(s);
            gtk_snapshot_pop(s);
            gtk_snapshot_pop(s);
            gtk_snapshot_pop(s);
            gsk_render_node_unref(under);
          } else {
          gtk_snapshot_push_clip(s, &shown);
          gtk_snapshot_save(s);
          gtk_snapshot_translate(s, &shift);
          appendPainted(s, paint(layers.size() + 1 + scrolls++, content.cells, true), &content.cells, content.row, content.col);
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
          // Past the screen's edge the picture is mirrored, so the edge blurs
          // with its own colours: nothing there would darken or lighten it
          // toward a vignette.
          float margin = static_cast<float>(3.0 * radius);
          float fw = static_cast<float>(width), fh = static_cast<float>(height);
          gtk_snapshot_push_clip(s, &region);
          gtk_snapshot_push_blur(s, radius);
          gtk_snapshot_append_node(s, composed);
          for (int iy = -1; iy <= 1; iy++) {
            for (int ix = -1; ix <= 1; ix++) {
              if (ix == 0 && iy == 0) continue;
              // The strip outside the screen on that side, and the mirror
              // about the edge it borders.
              float x0 = ix < 0 ? -margin : ix > 0 ? fw : 0, x1 = ix < 0 ? 0 : ix > 0 ? fw + margin : fw;
              float y0 = iy < 0 ? -margin : iy > 0 ? fh : 0, y1 = iy < 0 ? 0 : iy > 0 ? fh + margin : fh;
              graphene_rect_t strip = GRAPHENE_RECT_INIT(x0, y0, x1 - x0, y1 - y0);
              gtk_snapshot_save(s);
              gtk_snapshot_push_clip(s, &strip);
              graphene_point_t about = GRAPHENE_POINT_INIT(ix > 0 ? 2 * fw : 0.0f, iy > 0 ? 2 * fh : 0.0f);
              gtk_snapshot_translate(s, &about);
              gtk_snapshot_scale(s, ix != 0 ? -1.0f : 1.0f, iy != 0 ? -1.0f : 1.0f);
              gtk_snapshot_append_node(s, composed);
              gtk_snapshot_pop(s);
              gtk_snapshot_restore(s);
            }
          }
          gtk_snapshot_pop(s);
          gtk_snapshot_pop(s);
        }
        if (layer.backdrop.dim > 0.0f) {
          GdkRGBA tint = channel(layer.backdrop.tint.value_or(bg), layer.backdrop.dim);
          gtk_snapshot_append_color(s, &tint, &region);
        }
        gsk_render_node_unref(composed);
        below = gtk_snapshot_free_to_node(s);
        stacks.push_back(below);
      }
      gtk_snapshot_append_node(snap, below);
      const Painted & top = paint(layers.size(), frame, true);
      appendPainted(snap, top, &frame);
      appendShadows(snap, top, below, stacks);
      for (GskRenderNode * node : stacks) gsk_render_node_unref(node);
    }
    // Layers that are gone leave their textures.
    size_t used = layers.size() + 1 + scrolls;
    for (size_t i = used; i < painted.size(); i++) release(painted[i]);
    painted.resize(used);
    // Pictures nothing shows any more give up their textures.
    for (auto it = image_textures.begin(); it != image_textures.end();) {
      if (it->second.used) {
        it->second.used = false;
        ++it;
        continue;
      }
      if (it->second.texture) g_object_unref(it->second.texture);
      it = image_textures.erase(it);
    }
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
  if (impl_->tick_id) gtk_widget_remove_tick_callback(impl_->view, impl_->tick_id);
  if (impl_->view) reinterpret_cast<CelluiView *>(impl_->view)->impl = nullptr;
  if (impl_->window) gtk_window_destroy(GTK_WINDOW(impl_->window));
  impl_->forgetTextures();
  if (impl_->proportional) pango_font_description_free(impl_->proportional);
  for (auto & entry : impl_->glyph_nodes) {
    if (entry.second) gsk_render_node_unref(entry.second);
  }
  for (PangoFontDescription * font : impl_->cell_fonts) {
    if (font) pango_font_description_free(font);
  }
  if (impl_->text_context) g_object_unref(impl_->text_context);
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
  impl_->text_runs = info ? info->text_runs : std::vector<TextRun>();
  std::stable_sort(impl_->elevations.begin(), impl_->elevations.end(),
                   [](const ElevatedArea & a, const ElevatedArea & b) { return a.elevation < b.elevation; });
  if (info) {
    impl_->mouse_cursors = info->mouse_cursors;
    impl_->updateMouseCursor();
  }
  // Inside a tick the tick draws; anywhere else the draw is asked for here
  // (GTK folds the requests of one frame into one).
  if (impl_->in_tick) impl_->presented_in_tick = true;
  else gtk_widget_queue_draw(impl_->view);
}

void Window::requestFrame() {
  impl_->frame_dirty = true;
  impl_->idle_ticks = 0;
  if (!impl_->tick_id && impl_->view) impl_->tick_id = gtk_widget_add_tick_callback(impl_->view, Impl::onTick, impl_.get(), nullptr);
}

void Window::setFrameHandler(FrameHandler handler) { impl_->frame_handler = std::move(handler); }

LinkStatus Window::linkStatus() const {
  LinkStatus status = impl_->link.status();
  if (status.description.empty()) status.description = impl_->vsyncText();
  return status;
}
void Window::setLinkHandler(LinkHandler handler) { impl_->link.setHandler(std::move(handler)); }
void Window::setLinkPin(std::optional<LinkClass> pin) { impl_->link.pin(pin); }

void Window::redraw() {
  impl_->forgetTextures();
  gtk_widget_queue_draw(impl_->view);
}

void Window::wake() { impl_->signal(impl_->wake_fd); }

}  // namespace cellui::gtk
