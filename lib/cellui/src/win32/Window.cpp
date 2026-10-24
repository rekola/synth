#include <cellui/win32/Window.h>

#include <cellui/win32/Win32EventLoop.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace cellui::win32 {

namespace {

// Releases a COM object when it goes out of scope.
template <class T>
class Com {
 public:
  Com() = default;
  Com(const Com &) = delete;
  Com & operator=(const Com &) = delete;
  ~Com() { reset(); }
  void reset() {
    if (p_) p_->Release();
    p_ = nullptr;
  }
  T ** put() {
    reset();
    return &p_;
  }
  T * get() const { return p_; }
  T * operator->() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }

 private:
  T * p_ = nullptr;
};

std::wstring widen(const std::string & s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const wchar_t * w, int length) {
  if (length <= 0) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, length, nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, length, s.data(), n, nullptr, nullptr);
  return s;
}

D2D1_COLOR_F colorOf(const Color & c, float alpha = 1.0f) {
  return D2D1::ColorF(c.getRed() / 255.0f, c.getGreen() / 255.0f, c.getBlue() / 255.0f, alpha);
}

// IDC_ARROW (32512), IDC_IBEAM (32513) and IDC_HAND (32649): the macros are
// narrow strings unless UNICODE is defined.
HCURSOR systemCursor(int id) { return LoadCursorW(nullptr, MAKEINTRESOURCEW(id)); }

unsigned currentMods() {
  unsigned mods = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) mods |= kModShift;
  if (GetKeyState(VK_CONTROL) & 0x8000) mods |= kModCtrl;
  if (GetKeyState(VK_MENU) & 0x8000) mods |= kModAlt;
  if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= kModSuper;
  return mods;
}

// The key id of a virtual key that makes no character, 0 for a modifier or
// lock on its own, -1 for one that does (the caller asks the keyboard layout).
int specialKey(WPARAM vk, bool extended) {
  if (vk >= VK_F1 && vk <= VK_F24) return Key::F1 + static_cast<int>(vk - VK_F1);
  switch (vk) {
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_INSERT: return Key::Insert;
    case VK_DELETE: return Key::Delete;
    case VK_BACK: return Key::Backspace;
    case VK_RETURN: return Key::Enter;
    case VK_TAB: return Key::Tab;
    case VK_ESCAPE: return Key::Escape;
    case VK_APPS: return Key::Menu;
    case VK_DIVIDE: return Key::KpDivide;
    case VK_MULTIPLY: return Key::KpMultiply;
    case VK_PAUSE: return Key::Pause;
    case VK_SNAPSHOT: return Key::PrintScreen;
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
      return 0;
    default: break;
  }
  (void)extended;
  return -1;
}

// The character a key makes under the keyboard layout and the shift/caps
// state, 0 when none (a dead key or a key without one). Ctrl and Alt are
// taken out, so Ctrl+A is 'a' and the modifier stays a modifier; with AltGr
// (reported as Ctrl+Alt) they are left in, so the layout's AltGr character
// is found, and then `altgr` is set.
int characterFor(WPARAM vk, LPARAM lparam, bool & altgr) {
  BYTE state[256];
  if (!GetKeyboardState(state)) return 0;
  UINT scan = (static_cast<UINT>(lparam) >> 16) & 0xff;
  HKL layout = GetKeyboardLayout(0);
  wchar_t out[4] = {};
  // Flag 4: leave the keyboard's dead-key state alone.
  auto convert = [&](const BYTE * keys) { return ToUnicodeEx(static_cast<UINT>(vk), scan, keys, out, 4, 4, layout); };

  altgr = false;
  if ((state[VK_CONTROL] & 0x80) && (state[VK_MENU] & 0x80)) {
    int n = convert(state);
    if (n >= 1 && out[0] >= 0x20) {
      altgr = true;
      return out[0];
    }
  }
  state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_RCONTROL] = 0;
  state[VK_MENU] = state[VK_LMENU] = state[VK_RMENU] = 0;
  int n = convert(state);
  if (n < 1) return 0;
  if (n >= 2 && out[0] >= 0xd800 && out[0] < 0xdc00) return 0x10000 + ((out[0] - 0xd800) << 10) + (out[1] - 0xdc00);
  return out[0];
}

}  // namespace

struct Window::Impl {
  Window * owner = nullptr;
  Win32EventLoop loop;
  HWND hwnd = nullptr;
  HANDLE input_event = nullptr, wake_event = nullptr;

  Com<ID2D1Factory> d2d;
  Com<IDWriteFactory> dwrite;
  Com<ID2D1HwndRenderTarget> target;
  Com<IDWriteTextFormat> formats[4];  // by bold | italic << 1
  Com<ID2D1SolidColorBrush> brush;

  WindowOptions options;
  DisplayInfo display_info;
  UINT dpi = 96;
  int rows = 0, cols = 0;
  int cell_h = 0, cell_w = 0;
  float baseline = 0;  // from a cell's top to the text's baseline
  int client_w = 0, client_h = 0;

  std::deque<BackendEvent> queue;

  // The frame being shown: what lies above the backdrops, and the layers
  // below each of them.
  CellBuffer frame{ 0, 0 };
  CursorRequest cursor;
  std::vector<ImagePlacement> images;
  std::vector<BackdropLayer> layers;
  std::vector<ElevatedArea> elevations;
  std::vector<CursorArea> mouse_cursors;

  // The Direct2D bitmaps of pictures, by plane key, kept while the version
  // is the same.
  struct Bitmap {
    uint64_t version = 0;
    Com<ID2D1Bitmap> bitmap;
  };
  std::map<uint64_t, Bitmap> bitmaps;

  int held_button = 0;
  // The keys that are down, by virtual key, with the id they were sent as,
  // so losing the focus can let them go.
  std::map<WPARAM, int> held_keys;
  int last_row = -1, last_col = -1;
  // The pointer's position in cells, with the fraction.
  float last_fy = -1, last_fx = -1;
  double wheel_x = 0, wheel_y = 0;
  bool tracking = false;

  void push(BackendEvent event) {
    queue.push_back(std::move(event));
    SetEvent(input_event);
  }
  // The position under a pixel in cells, with the fraction, clamped to the grid.
  void cellPosition(int x, int y, float & fy, float & fx) const {
    float max_x = std::max(0.0f, static_cast<float>(cols) - 0.001f), max_y = std::max(0.0f, static_cast<float>(rows) - 0.001f);
    fx = std::clamp(static_cast<float>(x) / static_cast<float>(std::max(cell_w, 1)), 0.0f, max_x);
    fy = std::clamp(static_cast<float>(y) / static_cast<float>(std::max(cell_h, 1)), 0.0f, max_y);
  }
  void cellAt(int x, int y, int & row, int & col) const {
    col = std::clamp(x / std::max(cell_w, 1), 0, std::max(0, cols - 1));
    row = std::clamp(y / std::max(cell_h, 1), 0, std::max(0, rows - 1));
  }

  bool createDevice();
  void makeFonts();
  void applyTheme();
  void resized(int width, int height);
  void dpiChanged(UINT new_dpi, const RECT * suggested);

  void paint();
  // The cells drawing may be limited to (screen cells).
  struct CellRange {
    int row, col, rows, cols;
  };
  // What drawing goes to: the window's target, or an offscreen one while
  // a backdrop blurs what is below it.
  ID2D1RenderTarget * rt_ = nullptr;
  void drawCells(const CellBuffer & cells, bool transparent, int origin_row, int origin_col, double shift_x, double shift_y,
                 const CellRange * only = nullptr);
  void drawShadows();
  void drawLayer(size_t i, const CellRange * only);
  void drawLayerDim(size_t i);
  void blurRegion(Com<ID2D1BitmapRenderTarget> & below, const D2D1_RECT_F & region, float radius);
  ID2D1Bitmap * bitmapFor(const ImagePlacement & placement);

  LRESULT message(UINT msg, WPARAM wparam, LPARAM lparam);
  bool keyEvent(WPARAM vk, LPARAM lparam, bool pressed);
  void mouseButton(int button, bool pressed, LPARAM lparam);
  void mouseMove(LPARAM lparam);
  void mouseWheel(bool horizontal, int delta, LPARAM lparam);
  void pasteClipboard();
  void focusChanged(bool focused);
  void updateCursor();
  static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
};

bool Window::Impl::createDevice() {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void **>(d2d.put())))) return false;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(dwrite.put())))) return false;
  return true;
}

// The four text formats and the cell size they give: the width of an "M"
// and the font's line height, in pixels at the monitor's DPI.
void Window::Impl::makeFonts() {
  float size = static_cast<float>(options.font_points * dpi / 72.0);
  std::wstring family = widen(options.font);
  for (int i = 0; i < 4; i++) {
    DWRITE_FONT_WEIGHT weight = (i & 1) ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = (i & 2) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    dwrite->CreateTextFormat(family.c_str(), nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL, size, L"", formats[i].put());
    if (formats[i]) {
      formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
  }
  cell_w = 8;
  cell_h = 16;
  if (!formats[0]) return;
  Com<IDWriteTextLayout> layout;
  if (SUCCEEDED(dwrite->CreateTextLayout(L"M", 1, formats[0].get(), 1000.0f, 1000.0f, layout.put()))) {
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    DWRITE_LINE_METRICS line;
    UINT32 count = 1;
    layout->GetLineMetrics(&line, 1, &count);
    cell_w = std::max(static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace)), 4);
    cell_h = std::max(static_cast<int>(std::ceil(line.height)), 8);
    baseline = line.baseline;
  }
}

// Default colours follow the system's app theme; the title bar follows too.
void Window::Impl::applyTheme() {
  DWORD light = 1, size = sizeof light;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD,
               nullptr, &light, &size);
  bool dark = light == 0;
  display_info.default_fg = dark ? Color(0xe6, 0xe6, 0xe6) : Color(0x20, 0x20, 0x20);
  display_info.default_bg = dark ? Color(0x1e, 0x1e, 0x1e) : Color(0xfa, 0xfa, 0xfa);
  // Silently absent on a Windows without it.
  BOOL value = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof value);
}

void Window::Impl::resized(int width, int height) {
  client_w = width;
  client_h = height;
  if (target) target->Resize(D2D1::SizeU(static_cast<UINT32>(std::max(width, 1)), static_cast<UINT32>(std::max(height, 1))));
  int new_cols = std::max(1, width / cell_w), new_rows = std::max(1, height / cell_h);
  if (new_cols == cols && new_rows == rows) return;
  cols = new_cols;
  rows = new_rows;
  push(BackendEvent::makeResize(rows, cols));
}

void Window::Impl::dpiChanged(UINT new_dpi, const RECT * suggested) {
  dpi = new_dpi;
  makeFonts();
  display_info.cell_px_height = cell_h;
  display_info.cell_px_width = cell_w;
  display_info.display_scale = dpi / 96.0;
  bitmaps.clear();
  if (suggested) {
    SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  RECT client;
  GetClientRect(hwnd, &client);
  resized(client.right, client.bottom);
  // The size in cells may be the same at the new cell size; the application
  // still has to draw at the new pixel size.
  push(BackendEvent::makeResize(rows, cols));
}

// A picture's Direct2D bitmap, made again when its version changed.
ID2D1Bitmap * Window::Impl::bitmapFor(const ImagePlacement & placement) {
  if (!placement.image || !target) return nullptr;
  Bitmap & b = bitmaps[placement.key];
  if (b.bitmap && b.version == placement.image->version) return b.bitmap.get();
  const Image & image = *placement.image;
  if (image.width <= 0 || image.height <= 0 || image.pixels.size() < static_cast<size_t>(image.width) * static_cast<size_t>(image.height)) return nullptr;
  D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  b.bitmap.reset();
  if (FAILED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(image.width), static_cast<UINT32>(image.height)), image.pixels.data(),
                                  static_cast<UINT32>(image.width) * 4, props, b.bitmap.put())))
    return nullptr;
  b.version = image.version;
  return b.bitmap.get();
}

void Window::Impl::drawCells(const CellBuffer & cells, bool transparent, int origin_row, int origin_col, double shift_x, double shift_y, const CellRange * only) {
  const Color default_fg = *display_info.default_fg;
  const float w = static_cast<float>(cell_w), h = static_cast<float>(cell_h);
  int r0 = 0, r1 = cells.rows(), c0 = 0, c1 = cells.cols();
  if (only) {
    r0 = std::max(r0, only->row - origin_row);
    r1 = std::min(r1, only->row + only->rows - origin_row);
    c0 = std::max(c0, only->col - origin_col);
    c1 = std::min(c1, only->col + only->cols - origin_col);
  }
  for (int r = r0; r < r1; r++) {
    for (int c = c0; c < c1; c++) {
      const CellBuffer::Cell & cell = cells.at(r, c);
      if (transparent && CellBuffer::isBlank(cell)) continue;
      float x = static_cast<float>((origin_col + c) * cell_w - shift_x), y = static_cast<float>((origin_row + r) * cell_h - shift_y);
      if (cell.bg) {
        brush->SetColor(colorOf(*cell.bg));
        rt_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush.get());
      }
      if (cell.image > 0 && static_cast<size_t>(cell.image) <= images.size()) {
        const ImagePlacement & p = images[static_cast<size_t>(cell.image - 1)];
        if (ID2D1Bitmap * bitmap = bitmapFor(p)) {
          const Image & image = *p.image;
          int lo_x, hi_x, lo_y, hi_y;
          image.pixelSpan(origin_col + c - p.col, 1, cell_w, true, lo_x, hi_x);
          image.pixelSpan(origin_row + r - p.row, 1, cell_h, false, lo_y, hi_y);
          if (hi_x > lo_x && hi_y > lo_y) {
            rt_->DrawBitmap(bitmap, D2D1::RectF(x, y, x + w, y + h), 1.0f,
                               image.stretched() ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                               D2D1::RectF(static_cast<float>(lo_x), static_cast<float>(lo_y), static_cast<float>(hi_x), static_cast<float>(hi_y)));
          }
        }
        continue;
      }
      if (cell.continuation) continue;
      Color fg = cell.fg ? *cell.fg : default_fg;
      bool blank = cell.text.empty() || cell.text == " ";
      if (!blank) {
        // A double-width character spans its continuation cell too.
        bool wide = c + 1 < cells.cols() && cells.at(r, c + 1).continuation;
        std::wstring text = widen(cell.text);
        IDWriteTextFormat * format = formats[(cell.bold ? 1 : 0) | (cell.italic ? 2 : 0)].get();
        if (format) {
          brush->SetColor(colorOf(fg));
          D2D1_RECT_F box = D2D1::RectF(x, y, x + (wide ? 2 * w : w), y + h);
          rt_->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
          rt_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, D2D1::RectF(x, y, x + 4 * w, y + 2 * h), brush.get(),
                           D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
          rt_->PopAxisAlignedClip();
        }
      }
      if (cell.underline) {
        bool wide = c + 1 < cells.cols() && cells.at(r, c + 1).continuation;
        brush->SetColor(colorOf(fg));
        float line = y + h - std::max(1.0f, h / 12.0f);
        rt_->FillRectangle(D2D1::RectF(x, line, x + (wide ? 2 * w : w), line + std::max(1.0f, h / 16.0f)), brush.get());
      }
    }
  }
}

// What lies below a blurring backdrop, blurred: the region is drawn small
// and then drawn large again, which smooths it by about the factor. (The
// window's target has no effects to blur with.) `below` is drawn on again
// afterwards.
void Window::Impl::blurRegion(Com<ID2D1BitmapRenderTarget> & below, const D2D1_RECT_F & region, float radius) {
  float width = region.right - region.left, height = region.bottom - region.top;
  if (width < 1 || height < 1) return;
  float factor = std::clamp(radius / 2.0f, 2.0f, 16.0f);
  UINT32 small_w = static_cast<UINT32>(std::max(1.0f, std::ceil(width / factor))), small_h = static_cast<UINT32>(std::max(1.0f, std::ceil(height / factor)));
  below->EndDraw();
  Com<ID2D1Bitmap> bitmap;
  Com<ID2D1BitmapRenderTarget> small;
  if (SUCCEEDED(below->GetBitmap(bitmap.put())) && SUCCEEDED(below->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(small_w), static_cast<float>(small_h)),
                                                                                                   D2D1::SizeU(small_w, small_h), small.put()))) {
    small->BeginDraw();
    small->DrawBitmap(bitmap.get(), D2D1::RectF(0, 0, static_cast<float>(small_w), static_cast<float>(small_h)), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, region);
    small->EndDraw();
    Com<ID2D1Bitmap> reduced;
    if (SUCCEEDED(small->GetBitmap(reduced.put()))) {
      below->BeginDraw();
      below->DrawBitmap(reduced.get(), region, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      return;
    }
  }
  below->BeginDraw();
}

// A soft shadow around every plane above the base that nothing blurs, the
// higher the softer and the further down: rings that fade outwards, drawn
// over the frame outside the plane, and under every plane higher than it,
// which is drawn again over it. Height is in cell widths.
void Window::Impl::drawShadows() {
  if (elevations.empty()) return;
  const Color & bg = *display_info.default_bg;
  bool dark = bg.luminance() < 0.2f;
  float key_alpha = dark ? 0.65f : 0.18f, ambient_alpha = dark ? 0.4f : 0.12f;
  auto rectOf = [&](const ElevatedArea & a) {
    return D2D1::RectF(static_cast<float>(a.col * cell_w), static_cast<float>(a.row * cell_h), static_cast<float>((a.col + a.cols) * cell_w),
                       static_cast<float>((a.row + a.rows) * cell_h));
  };
  auto ring = [&](const D2D1_RECT_F & r, float dx, float dy, float reach, float alpha) {
    // Steps of about 1.5 px; the strength falls off as a smooth bell.
    int steps = std::clamp(static_cast<int>(reach / 1.5f), 1, 40);
    float step = reach / static_cast<float>(steps);
    for (int j = steps; j >= 1; j--) {
      float t = static_cast<float>(j - 1) / static_cast<float>(steps);
      float a = alpha * (1.0f - t) * (1.0f - t) / static_cast<float>(steps) * 2.2f;
      brush->SetColor(D2D1::ColorF(0, 0, 0, std::min(a, 1.0f)));
      float grow = static_cast<float>(j) * step - step / 2;
      rt_->DrawRectangle(D2D1::RectF(r.left + dx - grow, r.top + dy - grow, r.right + dx + grow, r.bottom + dy + grow), brush.get(), step);
    }
  };
  // The lifted bands of scrolled planes, which are drawn in the layers below.
  struct LiftedBand {
    D2D1_RECT_F rect;
    CellRange cells;
    float elevation;
  };
  std::vector<LiftedBand> bands;
  for (const BackdropLayer & layer : layers) {
    if (!layer.scroll || !layer.scroll->band || layer.scroll->band->lift <= 0.0f) continue;
    const ScrollContent & content = *layer.scroll;
    const Band & band = *content.band;
    float left = static_cast<float>(std::max(static_cast<double>(content.col), content.col + band.col) * cell_w);
    float top = static_cast<float>(std::max(static_cast<double>(content.row), content.row + band.row) * cell_h);
    float right = static_cast<float>(std::min(static_cast<double>(content.col + content.cols), content.col + band.col + band.cols) * cell_w);
    float bottom = static_cast<float>(std::min(static_cast<double>(content.row + content.rows), content.row + band.row + band.rows) * cell_h);
    if (right <= left || bottom <= top) continue;
    int r0 = static_cast<int>(top) / cell_h, c0 = static_cast<int>(left) / cell_w;
    int r1 = (static_cast<int>(std::ceil(bottom)) + cell_h - 1) / cell_h, c1 = (static_cast<int>(std::ceil(right)) + cell_w - 1) / cell_w;
    bands.push_back(LiftedBand{ D2D1::RectF(left, top, right, bottom), { r0, c0, r1 - r0, c1 - c0 }, content.elevation + band.lift });
  }
  size_t i = 0;
  while (i < elevations.size()) {
    float e = elevations[i].elevation;
    size_t level_end = i;
    while (level_end < elevations.size() && elevations[level_end].elevation == e) level_end++;
    float px = e * static_cast<float>(cell_w);
    float dx = 0.3f * px, dy = 0.5f * px;
    bool any = false;
    for (size_t k = i; k < level_end; k++) {
      if (elevations[k].layer < layers.size()) continue;  // a later backdrop covers it
      if (!elevations[k].shadow) continue;                  // stands above lower shadows, casts none
      D2D1_RECT_F r = rectOf(elevations[k]);
      ring(r, 0, 0, std::max(2.0f, 0.25f * px), ambient_alpha);
      ring(r, dx, dy, px, key_alpha);
      any = true;
    }
    if (any) {
      // A lifted band again, with what lies below and above it (unblurred).
      for (const LiftedBand & band : bands) {
        if (band.elevation < e) continue;
        rt_->PushAxisAlignedClip(band.rect, D2D1_ANTIALIAS_MODE_ALIASED);
        rt_->Clear(colorOf(bg));
        for (size_t k = 0; k < layers.size(); k++) {
          drawLayer(k, &band.cells);
          drawLayerDim(k);
        }
        drawCells(frame, !layers.empty(), 0, 0, 0, 0, &band.cells);
        rt_->PopAxisAlignedClip();
      }
      // Everything as high or higher is drawn again over the shadows.
      for (size_t j = i; j < elevations.size(); j++) {
        const ElevatedArea & a = elevations[j];
        if (a.layer < layers.size()) continue;
        rt_->PushAxisAlignedClip(rectOf(a), D2D1_ANTIALIAS_MODE_ALIASED);
        CellRange range = { a.row, a.col, a.rows, a.cols };
        drawCells(frame, !layers.empty(), 0, 0, 0, 0, &range);
        rt_->PopAxisAlignedClip();
      }
      // A light top edge and a darker bottom one.
      for (size_t k = i; k < level_end; k++) {
        if (elevations[k].layer < layers.size() || !elevations[k].shadow) continue;
        D2D1_RECT_F r = rectOf(elevations[k]);
        brush->SetColor(D2D1::ColorF(1, 1, 1, dark ? 0.14f : 0.55f));
        rt_->FillRectangle(D2D1::RectF(r.left, r.top, r.right, r.top + 1), brush.get());
        brush->SetColor(D2D1::ColorF(0, 0, 0, dark ? 0.3f : 0.1f));
        rt_->FillRectangle(D2D1::RectF(r.left, r.bottom - 1, r.right, r.bottom), brush.get());
      }
    }
    i = level_end;
  }
}

// A layer's cells and its scrolled plane with its band.
void Window::Impl::drawLayer(size_t i, const CellRange * only) {
  const BackdropLayer & layer = layers[i];
  drawCells(layer.cells, i > 0, 0, 0, 0, 0, only);
  if (!layer.scroll) return;
  const ScrollContent & content = *layer.scroll;
  D2D1_RECT_F shown = D2D1::RectF(static_cast<float>(content.col * cell_w), static_cast<float>(content.row * cell_h),
                                  static_cast<float>((content.col + content.cols) * cell_w), static_cast<float>((content.row + content.rows) * cell_h));
  rt_->PushAxisAlignedClip(shown, D2D1_ANTIALIAS_MODE_ALIASED);
  // The content is shifted by up to a cell, so one more of it may show.
  CellRange wider{};
  if (only) wider = { only->row, only->col, only->rows + 1, only->cols + 1 };
  drawCells(content.cells, true, content.row, content.col, content.shift_cols * cell_w, content.shift_rows * cell_h, only ? &wider : nullptr);
  if (content.band) {
    const Band & band = *content.band;
    // Blend modes are not drawn here yet: a blended band is a plain tint.
    brush->SetColor(colorOf(band.color, band.blend == BlendMode::NORMAL ? band.alpha : 0.45f));
    float bx = static_cast<float>((content.col + band.col) * cell_w), by = static_cast<float>((content.row + band.row) * cell_h);
    rt_->FillRectangle(D2D1::RectF(bx, by, bx + static_cast<float>(band.cols * cell_w), by + static_cast<float>(band.rows * cell_h)), brush.get());
    if (band.border_px > 0) {
      // Inside the rectangle: a stroke is centred on its path.
      float half = static_cast<float>(band.border_px) / 2.0f;
      brush->SetColor(colorOf(band.border_color, band.border_alpha));
      rt_->DrawRectangle(D2D1::RectF(bx + half, by + half, bx + static_cast<float>(band.cols * cell_w) - half, by + static_cast<float>(band.rows * cell_h) - half), brush.get(),
                         static_cast<float>(band.border_px));
    }
  }
  rt_->PopAxisAlignedClip();
}

void Window::Impl::drawLayerDim(size_t i) {
  const BackdropLayer & layer = layers[i];
  if (layer.backdrop.dim <= 0.0f) return;
  D2D1_RECT_F region = D2D1::RectF(static_cast<float>(layer.col * cell_w), static_cast<float>(layer.row * cell_h),
                                   static_cast<float>((layer.col + layer.cols) * cell_w), static_cast<float>((layer.row + layer.rows) * cell_h));
  brush->SetColor(colorOf(layer.backdrop.tint.value_or(*display_info.default_bg), layer.backdrop.dim));
  rt_->FillRectangle(region, brush.get());
}

void Window::Impl::paint() {
  if (!target || !brush) return;
  const Color bg = *display_info.default_bg;
  bool blurs = false;
  for (const BackdropLayer & layer : layers) blurs |= layer.backdrop.blur > 0;

  // With a blurring backdrop everything is drawn offscreen first, as the
  // blur reads what is below it.
  Com<ID2D1BitmapRenderTarget> offscreen;
  if (blurs && SUCCEEDED(target->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(client_w), static_cast<float>(client_h)),
                                                              D2D1::SizeU(static_cast<UINT32>(std::max(client_w, 1)), static_cast<UINT32>(std::max(client_h, 1))),
                                                              offscreen.put()))) {
    rt_ = offscreen.get();
  } else {
    blurs = false;
    rt_ = target.get();
  }
  rt_->BeginDraw();
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
  rt_->Clear(colorOf(bg));
  if (frame.rows() > 0 && frame.cols() > 0) {
    // Each backdrop blurs and dims what is below it, over the cells it covers.
    for (size_t i = 0; i < layers.size(); i++) {
      const BackdropLayer & layer = layers[i];
      drawLayer(i, nullptr);
      if (blurs && layer.backdrop.blur > 0) {
        D2D1_RECT_F region = D2D1::RectF(static_cast<float>(layer.col * cell_w), static_cast<float>(layer.row * cell_h),
                                         static_cast<float>((layer.col + layer.cols) * cell_w), static_cast<float>((layer.row + layer.rows) * cell_h));
        double half_row = std::min(layer.backdrop.blur, Backdrop::kMaxBlur) * cell_h / 2.0;
        blurRegion(offscreen, region, static_cast<float>(2.0 * std::sqrt(2.0 * half_row * (half_row + 1.0) / 3.0)));
      }
      drawLayerDim(i);
    }
    drawCells(frame, !layers.empty(), 0, 0, 0, 0);
    drawShadows();
  }
  if (blurs) {
    rt_->EndDraw();
    Com<ID2D1Bitmap> composed;
    offscreen->GetBitmap(composed.put());
    rt_ = target.get();
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (composed) rt_->DrawBitmap(composed.get());
  }
  if (cursor.visible) {
    brush->SetColor(colorOf(*display_info.default_fg));
    float x = static_cast<float>(cursor.x * cell_w), y = static_cast<float>(cursor.y * cell_h);
    rt_->FillRectangle(D2D1::RectF(x, y, x + 2.0f, y + static_cast<float>(cell_h)), brush.get());
  }
  HRESULT hr = target->EndDraw();
  rt_ = nullptr;
  if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
    // The device was lost (a display driver reset): make it again.
    brush.reset();
    target.reset();
    bitmaps.clear();
    RECT client;
    GetClientRect(hwnd, &client);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96.0f, 96.0f);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hw = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom)));
    if (SUCCEEDED(d2d->CreateHwndRenderTarget(rtp, hw, target.put()))) target->CreateSolidColorBrush(D2D1::ColorF(0), brush.put());
    InvalidateRect(hwnd, nullptr, FALSE);
  }
}

void Window::Impl::pasteClipboard() {
  if (!OpenClipboard(hwnd)) return;
  if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
    if (auto * text = static_cast<const wchar_t *>(GlobalLock(data))) {
      std::string utf8 = narrow(text, static_cast<int>(wcslen(text)));
      GlobalUnlock(data);
      // Clipboard text has CR LF line ends; the application reads LF.
      utf8.erase(std::remove(utf8.begin(), utf8.end(), '\r'), utf8.end());
      push(BackendEvent::makePaste(std::move(utf8)));
    }
  }
  CloseClipboard();
}

bool Window::Impl::keyEvent(WPARAM vk, LPARAM lparam, bool pressed) {
  unsigned mods = currentMods();
  bool extended = (lparam >> 24) & 1;
  if (pressed && ((vk == VK_INSERT && (mods & kModShift)) || (vk == 'V' && (mods & kModCtrl) && (mods & kModShift)))) {
    pasteClipboard();
    return true;
  }
  int key = specialKey(vk, extended);
  if (key == 0) return false;
  if (key < 0) {
    bool altgr = false;
    key = characterFor(vk, lparam, altgr);
    if (altgr) mods &= ~(kModCtrl | kModAlt);
    if (key == 0) return false;
  } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
    key = '0' + static_cast<int>(vk - VK_NUMPAD0);
  }
  Input::Kind kind = !pressed ? Input::Kind::RELEASE : ((lparam & (1 << 30)) ? Input::Kind::REPEAT : Input::Kind::PRESS);
  if (pressed) held_keys[vk] = key;
  else held_keys.erase(vk);
  push(BackendEvent::makeInput(keyInput(key, mods, kind)));
  return true;
}

// The release of a key or button that goes up while another window has the
// focus is never seen, so on losing it everything held is let go: a note key
// would otherwise sound until the window had the focus back.
void Window::Impl::focusChanged(bool focused) {
  if (!focused) {
    for (const auto & held : held_keys) push(BackendEvent::makeInput(keyInput(held.second, 0, Input::Kind::RELEASE)));
    held_keys.clear();
    if (held_button) {
      push(BackendEvent::makeInput(Input(Key::Button1 + held_button - 1, std::max(last_row, 0), std::max(last_col, 0), false, false, false, false,
                                         Input::Kind::RELEASE)));
      held_button = 0;
      if (GetCapture() == hwnd) ReleaseCapture();
    }
    wheel_x = wheel_y = 0;
  }
  push(BackendEvent::makeFocus(focused));
}

void Window::Impl::mouseButton(int button, bool pressed, LPARAM lparam) {
  int row, col;
  cellAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), row, col);
  float fy, fx;
  cellPosition(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), fy, fx);
  unsigned mods = currentMods();
  if (pressed) {
    held_button = button;
    SetCapture(hwnd);
    SetFocus(hwnd);
  } else {
    held_button = 0;
    ReleaseCapture();
  }
  last_row = row;
  last_col = col;
  last_fy = fy;
  last_fx = fx;
  push(BackendEvent::makeInput(Input(Key::Button1 + button - 1, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper),
                                     pressed ? Input::Kind::PRESS : Input::Kind::RELEASE)
                                   .withPosition(fy, fx)));
}

void Window::Impl::mouseMove(LPARAM lparam) {
  if (!tracking) {
    TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
    TrackMouseEvent(&t);
    tracking = true;
  }
  int row, col;
  cellAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), row, col);
  float fy, fx;
  cellPosition(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), fy, fx);
  if (fy == last_fy && fx == last_fx) return;
  last_row = row;
  last_col = col;
  last_fy = fy;
  last_fx = fx;
  updateCursor();
  unsigned mods = currentMods();
  // A drag repeats the press of the held button at the new position; with
  // no button the pointer is hovering.
  Input input = held_button ? Input(Key::Button1 + held_button - 1, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl,
                                    mods & (kModMeta | kModSuper), Input::Kind::PRESS)
                            : Input(Key::Motion, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper),
                                    Input::Kind::RELEASE);
  push(BackendEvent::makeInput(input.withPosition(fy, fx)));
}

void Window::Impl::mouseWheel(bool horizontal, int delta, LPARAM lparam) {
  // The wheel's position is the screen's.
  POINT p = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
  ScreenToClient(hwnd, &p);
  int row, col;
  cellAt(p.x, p.y, row, col);
  float fy, fx;
  cellPosition(p.x, p.y, fy, fx);
  // Up/right is positive in Windows; the buttons are 4 up, 5 down, 6 left,
  // 7 right.
  double & accumulated = horizontal ? wheel_x : wheel_y;
  accumulated += static_cast<double>(delta) / WHEEL_DELTA;
  unsigned mods = currentMods();
  auto emit = [&](int button) {
    push(BackendEvent::makeInput(
        Input(button, row, col, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper), Input::Kind::PRESS).withPosition(fy, fx)));
  };
  while (accumulated >= 1.0) {
    emit(horizontal ? Key::Button7 : Key::Button4);
    accumulated -= 1.0;
  }
  while (accumulated <= -1.0) {
    emit(horizontal ? Key::Button6 : Key::Button5);
    accumulated += 1.0;
  }
}

// Sets the pointer to the shape asked for over its cell.
void Window::Impl::updateCursor() {
  switch (mouseCursorAt(mouse_cursors, last_row, last_col)) {
    case MouseCursor::POINTER: SetCursor(systemCursor(32649)); break;
    case MouseCursor::TEXT: SetCursor(systemCursor(32513)); break;
    default: SetCursor(systemCursor(32512)); break;
  }
}

LRESULT Window::Impl::message(UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      paint();
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      if (wparam != SIZE_MINIMIZED) resized(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_DPICHANGED: dpiChanged(HIWORD(wparam), reinterpret_cast<const RECT *>(lparam)); return 0;
    case WM_SETFOCUS: focusChanged(true); return 0;
    case WM_KILLFOCUS: focusChanged(false); return 0;
    case WM_CAPTURECHANGED:
      // Capture taken away (another window, a dialog) ends a drag.
      if (held_button && reinterpret_cast<HWND>(lparam) != hwnd) {
        push(BackendEvent::makeInput(Input(Key::Button1 + held_button - 1, std::max(last_row, 0), std::max(last_col, 0), false, false, false, false,
                                           Input::Kind::RELEASE)));
        held_button = 0;
      }
      return 0;
    case WM_CLOSE:
      // The application ends the window by ending.
      push(BackendEvent::makeClose());
      return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      // Alt+F4 is the window's own.
      if (msg == WM_SYSKEYDOWN && wparam == VK_F4) break;
      // F10 and a bare Alt would open the system menu, taking the next key.
      keyEvent(wparam, lparam, true);
      return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP: keyEvent(wparam, lparam, false); return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      // Characters are made from the key messages.
      return 0;
    case WM_SYSCOMMAND:
      // Alt on its own would start the menu mode; Alt+Space is the window menu.
      if ((wparam & 0xfff0) == SC_KEYMENU && lparam != ' ') return 0;
      break;
    case WM_LBUTTONDOWN: mouseButton(1, true, lparam); return 0;
    case WM_LBUTTONUP: mouseButton(1, false, lparam); return 0;
    case WM_MBUTTONDOWN: mouseButton(2, true, lparam); return 0;
    case WM_MBUTTONUP: mouseButton(2, false, lparam); return 0;
    case WM_RBUTTONDOWN: mouseButton(3, true, lparam); return 0;
    case WM_RBUTTONUP: mouseButton(3, false, lparam); return 0;
    case WM_MOUSEMOVE: mouseMove(lparam); return 0;
    case WM_MOUSELEAVE: tracking = false; return 0;
    case WM_MOUSEWHEEL: mouseWheel(false, GET_WHEEL_DELTA_WPARAM(wparam), lparam); return 0;
    case WM_MOUSEHWHEEL: mouseWheel(true, GET_WHEEL_DELTA_WPARAM(wparam), lparam); return 0;
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT) {
        updateCursor();
        return TRUE;
      }
      break;
    case WM_SETTINGCHANGE:
      applyTheme();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK Window::Impl::proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto * create = reinterpret_cast<CREATESTRUCTW *>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto * self = reinterpret_cast<Impl *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
  return self->message(msg, wparam, lparam);
}

namespace {

constexpr wchar_t kClassName[] = L"cellui.Window";

bool registerClass() {
  static bool done = false;
  if (done) return true;
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof wc;
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = Window::Impl::proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = systemCursor(32512);
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc)) return false;
  done = true;
  return true;
}

// Per-monitor DPI awareness (v2) where the system has it; the functions are
// looked up, as older Windows does not export them.
void becomeDpiAware() {
  using SetContext = BOOL(WINAPI *)(HANDLE);
  if (auto set = reinterpret_cast<SetContext>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))))
    set(reinterpret_cast<HANDLE>(-4));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

UINT dpiOf(HWND hwnd) {
  using GetDpi = UINT(WINAPI *)(HWND);
  if (auto get = reinterpret_cast<GetDpi>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"))))
    return get(hwnd);
  return 96;
}

}  // namespace

std::unique_ptr<Window> Window::open(const WindowOptions & options, std::string * error) {
  auto fail = [&](const char * message) -> std::unique_ptr<Window> {
    if (error) *error = message;
    return nullptr;
  };
  becomeDpiAware();
  if (!registerClass()) return fail("cannot register the window class");

  std::unique_ptr<Window> w(new Window());
  Impl & impl = *w->impl_;
  impl.owner = w.get();
  impl.options = options;
  impl.input_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  impl.wake_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!impl.createDevice()) return fail("Direct2D or DirectWrite is not available");

  std::wstring title = widen(options.title);
  impl.hwnd = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr, nullptr,
                              GetModuleHandleW(nullptr), &impl);
  if (!impl.hwnd) return fail("cannot create a window (no desktop?)");

  impl.dpi = dpiOf(impl.hwnd);
  impl.makeFonts();
  if (!impl.formats[0]) return fail("the font cannot be loaded");
  impl.applyTheme();
  impl.display_info.cell_px_height = impl.cell_h;
  impl.display_info.cell_px_width = impl.cell_w;
  impl.display_info.pictures = true;
  impl.display_info.display_scale = impl.dpi / 96.0;
  impl.display_info.sextants = false;
  impl.display_info.smooth_scroll = true;
  impl.display_info.keymap_profile = KeymapProfile::DESKTOP;
  impl.display_info.escape_as_meta = false;
  impl.display_info.key_release = true;
  impl.display_info.precise_mouse = true;
  impl.display_info.hover = true;

  // The window's outer size for the wanted client size.
  impl.rows = options.rows;
  impl.cols = options.cols;
  impl.frame = CellBuffer(impl.rows, impl.cols);
  RECT outer = { 0, 0, impl.cols * impl.cell_w, impl.rows * impl.cell_h };
  AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
  SetWindowPos(impl.hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  RECT client;
  GetClientRect(impl.hwnd, &client);
  impl.client_w = client.right;
  impl.client_h = client.bottom;
  // Pixels are the unit: the target is 96 dpi whatever the monitor's.
  D2D1_RENDER_TARGET_PROPERTIES rt = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96.0f, 96.0f);
  D2D1_HWND_RENDER_TARGET_PROPERTIES hw =
      D2D1::HwndRenderTargetProperties(impl.hwnd, D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(client.right, 1)), static_cast<UINT32>(std::max<LONG>(client.bottom, 1))));
  if (FAILED(impl.d2d->CreateHwndRenderTarget(rt, hw, impl.target.put()))) return fail("cannot create a Direct2D render target");
  impl.target->CreateSolidColorBrush(D2D1::ColorF(0), impl.brush.put());
  // Text is drawn with ClearType where the system has it turned on.
  impl.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);

  // The size in cells follows the client area the window really got.
  impl.cols = std::max(1, static_cast<int>(client.right) / impl.cell_w);
  impl.rows = std::max(1, static_cast<int>(client.bottom) / impl.cell_h);
  impl.frame = CellBuffer(impl.rows, impl.cols);

  ShowWindow(impl.hwnd, SW_SHOW);
  SetForegroundWindow(impl.hwnd);
  SetFocus(impl.hwnd);
  return w;
}

Window::Window() : impl_(new Impl) { }

Window::~Window() {
  if (!impl_) return;
  if (impl_->hwnd) {
    SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, 0);
    DestroyWindow(impl_->hwnd);
  }
  if (impl_->input_event) CloseHandle(impl_->input_event);
  if (impl_->wake_event) CloseHandle(impl_->wake_event);
}

const DisplayInfo & Window::display() const { return impl_->display_info; }
int Window::rows() const { return impl_->rows; }
int Window::cols() const { return impl_->cols; }
EventLoop & Window::eventLoop() { return impl_->loop; }
void * Window::inputHandle() const { return impl_->input_event; }
void * Window::wakeHandle() const { return impl_->wake_event; }
void * Window::nativeHandle() const { return impl_->hwnd; }
void Window::wake() { SetEvent(impl_->wake_event); }

std::vector<BackendEvent> Window::poll() {
  ResetEvent(impl_->input_event);
  ResetEvent(impl_->wake_event);
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
  if (info) impl_->mouse_cursors = info->mouse_cursors;
  InvalidateRect(impl_->hwnd, nullptr, FALSE);
}

void Window::redraw() {
  impl_->bitmaps.clear();
  InvalidateRect(impl_->hwnd, nullptr, FALSE);
}

}  // namespace cellui::win32
