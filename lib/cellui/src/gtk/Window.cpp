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

  // The frame being shown, and the texture made from it when the widget last
  // drew it.
  CellBuffer frame{ 0, 0 };
  CursorRequest cursor;
  std::vector<ImagePlacement> images;
  GdkTexture * texture = nullptr;
  bool dirty = true;
  FrameStats stats;

  std::set<guint> held_keys;
  int held_button = 0;
  int last_row = -1, last_col = -1;
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

  void measure(const WindowOptions & options);
  void snapshot(GtkSnapshot * snapshot);
  void allocated(int width, int height);
  void pasteClipboard();
  bool keyEvent(guint keyval, GdkModifierType state, bool pressed, guint keycode);
  void buttonEvent(int button, double x, double y, bool pressed, GdkModifierType state);
  void motionEvent(double x, double y, GdkModifierType state);
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
}

void Window::Impl::allocated(int width, int height) {
  int new_cols = std::max(1, width / cell_w), new_rows = std::max(1, height / cell_h);
  if (new_cols == cols && new_rows == rows) return;
  cols = new_cols;
  rows = new_rows;
  push(BackendEvent::makeResize(rows, cols));
}

void Window::Impl::snapshot(GtkSnapshot * snap) {
  auto start = Clock::now();
  int width = gtk_widget_get_width(view), height = gtk_widget_get_height(view);
  auto channel = [](const Color & c) { return GdkRGBA{ c.getRed() / 255.0f, c.getGreen() / 255.0f, c.getBlue() / 255.0f, 1.0f }; };
  Color fg = *display_info.default_fg, bg = *display_info.default_bg;

  graphene_rect_t all = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(width), static_cast<float>(height));
  GdkRGBA background = channel(bg);
  gtk_snapshot_append_color(snap, &background, &all);

  if (dirty && frame.rows() > 0 && frame.cols() > 0 && rasterizer) {
    auto raster_start = Clock::now();
    Image image = rasterizer->render(frame, images, 0, 0, frame.rows(), frame.cols(), cell_h, cell_w, 1, fg, bg);
    if (image.width > 0 && image.height > 0) {
      // The pixels move into the texture's bytes; no copy.
      auto * pixels = new std::vector<uint32_t>(std::move(image.pixels));
      GBytes * bytes = g_bytes_new_with_free_func(
          pixels->data(), pixels->size() * sizeof(uint32_t), [](gpointer data) { delete static_cast<std::vector<uint32_t> *>(data); }, pixels);
      if (texture) g_object_unref(texture);
      texture = gdk_memory_texture_new(image.width, image.height, GDK_MEMORY_R8G8B8A8, bytes, static_cast<gsize>(image.width) * 4);
      g_bytes_unref(bytes);
    }
    dirty = false;
    double ms = msSince(raster_start);
    stats.raster_ms_total += ms;
    stats.raster_ms_max = std::max(stats.raster_ms_max, ms);
  }
  if (texture) {
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(gdk_texture_get_width(texture)), static_cast<float>(gdk_texture_get_height(texture)));
    gtk_snapshot_append_scaled_texture(snap, texture, GSK_SCALING_FILTER_NEAREST, &bounds);
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
  unsigned mods = modsFrom(state);
  held_button = pressed ? button : 0;
  last_row = row;
  last_col = col;
  Input input(Key::Button1 + button - 1, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper),
              pressed ? Input::Kind::PRESS : Input::Kind::RELEASE);
  push(BackendEvent::makeInput(input));
}

void Window::Impl::motionEvent(double x, double y, GdkModifierType state) {
  if (!held_button) return;
  int row, col;
  cellAt(x, y, row, col);
  if (row == last_row && col == last_col) return;
  last_row = row;
  last_col = col;
  unsigned mods = modsFrom(state);
  // A drag repeats the press of the held button at the new cell.
  Input input(Key::Button1 + held_button - 1, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper),
              Input::Kind::PRESS);
  push(BackendEvent::makeInput(input));
}

bool Window::Impl::scrollEvent(double dx, double dy, GdkModifierType state) {
  scroll_x += dx;
  scroll_y += dy;
  unsigned mods = modsFrom(state);
  int row = std::max(last_row, 0), col = std::max(last_col, 0);
  auto emit = [&](int button) {
    push(BackendEvent::makeInput(Input(button, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper), Input::Kind::PRESS)));
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
  if (impl_->texture) g_object_unref(impl_->texture);
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

void Window::present(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images, const FrameInfo *) {
  impl_->frame = frame;
  impl_->cursor = cursor;
  impl_->images = images;
  impl_->dirty = true;
  gtk_widget_queue_draw(impl_->view);
}

void Window::redraw() {
  impl_->dirty = true;
  gtk_widget_queue_draw(impl_->view);
}

void Window::wake() { impl_->signal(impl_->wake_fd); }

}  // namespace cellui::gtk
