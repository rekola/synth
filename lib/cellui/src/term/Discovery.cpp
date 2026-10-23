#include <cellui/term/Discovery.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace cellui::term {

namespace {

bool startsWith(const std::string & s, const char * prefix) {
  return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

bool contains(const std::string & s, const char * part) { return s.find(part) != std::string::npos; }

// Compares dotted version numbers field by field: -1, 0 or 1.
int compareVersions(const std::string & a, const std::string & b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    long x = 0, y = 0;
    while (i < a.size() && a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i++] - '0');
    while (j < b.size() && b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j++] - '0');
    if (x != y) return x < y ? -1 : 1;
    while (i < a.size() && (a[i] < '0' || a[i] > '9')) i++;
    while (j < b.size() && (b[j] < '0' || b[j] > '9')) j++;
  }
  return 0;
}

bool atLeast(const std::string & version, const char * minimum) {
  return !version.empty() && compareVersions(version, minimum) >= 0;
}

std::string hexDecode(const std::string & hex) {
  std::string out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16));
  return out;
}

std::string upper(std::string s) {
  for (char & c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
  return s;
}

// The colour count a terminfo entry of this name declares, for the common
// names; 8 for anything else that addresses the cursor.
int colorsForTerm(const std::string & term) {
  if (term.empty() || term == "dumb" || startsWith(term, "vt1") || startsWith(term, "vt2")) return 0;
  if (contains(term, "direct") || contains(term, "truecolor") || contains(term, "24bit")) return 1 << 24;
  if (contains(term, "256color") || term == "xterm-kitty" || term == "foot" || startsWith(term, "foot-") ||
      term == "alacritty" || term == "wezterm" || term == "contour" || term == "xterm-ghostty") return 256;
  return 8;
}

}  // namespace

Environment Environment::fromProcess() {
  auto get = [](const char * name) {
    const char * v = std::getenv(name);
    return std::string(v ? v : "");
  };
  Environment env;
  env.term = get("TERM");
  env.colorterm = get("COLORTERM");
  env.term_program = get("TERM_PROGRAM");
  env.konsole = !get("KONSOLE_VERSION").empty();
  env.ssh = !get("SSH_CONNECTION").empty() || !get("SSH_TTY").empty();
  env.colors_override = get("CELLUI_COLORS");
  env.pixels_override = get("CELLUI_PIXELS");
#ifdef _WIN32
  env.windows = true;
#endif
  return env;
}

double estimateDisplayScale(int cell_px_height) {
  if (cell_px_height <= 0) return 1.0;
  constexpr double kCellHeightAtScale1 = 18.0;
  double scale = std::round(cell_px_height / kCellHeightAtScale1 * 4.0) / 4.0;
  return std::clamp(scale, 1.0, 4.0);
}

namespace {

// WCAG relative luminance, 0 (black) to 1 (white).
double luminance(const Color & c) {
  auto l = c.linearSrgb();
  return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2];
}

}  // namespace

Appearance detectAppearance(const std::optional<Color> & fg, const std::optional<Color> & bg) {
  if (!bg) return Appearance::DARK;
  double b = luminance(*bg);
  if (fg) return b > luminance(*fg) ? Appearance::LIGHT : Appearance::DARK;
  // Black and white contrast equally with a luminance of about 0.18.
  return b > 0.179 ? Appearance::LIGHT : Appearance::DARK;
}

Discovery::Discovery(Environment env) : env_(std::move(env)) { }

bool Discovery::preIdentified() const {
  return env_.linux_console || startsWith(env_.term, "rxvt") || env_.term_program == "Apple_Terminal";
}

std::chrono::milliseconds Discovery::timeout() const {
  return std::chrono::milliseconds(env_.ssh ? 2000 : 1000);
}

std::string Discovery::queries() const {
  std::string q;
  q += "\x1b[6n";                // cursor position
  q += "\x1b[>u\x1b[=27u";        // push kitty keyboard flags: disambiguate, event types, all keys, text
  if (!preIdentified()) {
    q += "\x1b[=c";                                  // DA3: unit id
    q += "\x1b[>0q";                                 // XTVERSION
    q += "\x1bP+q544e;524742;687061\x1b\\";          // XTGETTCAP TN, RGB, hpa
    q += "\x1b[>c";                                  // DA2
  }
  if (!env_.linux_console) q += "\x1b]10;?\x1b\\\x1b]11;?\x1b\\";  // default colours
  q += "\x1b[?u";                 // kitty keyboard flags (answered only where supported)
  q += "\x1b[?2026$p";            // synchronized output
  q += "\x1b[?1;3;1024S";         // ask for 1024 Sixel colour registers
  if (!env_.windows) q += "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\";  // kitty graphics
  q += "\x1b[?2;1;0S\x1b[?1;1;0S";  // Sixel geometry and registers
  q += "\x1b[14t\x1b[16t\x1b[18t";  // text area in pixels, cell in pixels, size in cells
  q += "\x1b[c";                  // DA1, the sentinel
  return q;
}

void Discovery::ingest(const TermEvent & e) {
  if (e.type != TermEvent::Type::REPLY || complete_) return;
  auto param = [&](size_t i) { return i < e.params.size() ? e.params[i] : -1; };
  switch (e.reply) {
    case ReplyKind::TERTIARY_DA: {
      std::string id = upper(e.text);
      if (id == "7E565445") da3_name_ = "vte";
      else if (id == "464F4F54") da3_name_ = "foot";
      else if (id == "7E4B4445") da3_name_ = "konsole";
      else if (id == "7E7E5459") da3_name_ = "terminology";
      break;
    }
    case ReplyKind::XTVERSION: {
      const std::string & t = e.text;
      struct Prefix { const char * text; const char * name; bool parens; };
      static const Prefix prefixes[] = {
        {"XTerm(", "xterm", true}, {"WezTerm ", "wezterm", false}, {"contour ", "contour", false},
        {"kitty(", "kitty", true}, {"foot(", "foot", true}, {"mlterm(", "mlterm", true},
        {"tmux ", "tmux", false}, {"iTerm2 ", "iterm2", false}, {"mintty ", "mintty", false},
        {"terminology ", "terminology", false},
      };
      for (const Prefix & p : prefixes) {
        if (!startsWith(t, p.text)) continue;
        xtversion_name_ = p.name;
        std::string rest = t.substr(std::char_traits<char>::length(p.text));
        if (p.parens && !rest.empty() && rest.back() == ')') rest.pop_back();
        xtversion_version_ = rest;
        break;
      }
      break;
    }
    case ReplyKind::XTGETTCAP: {
      if (param(0) != 1) break;
      std::string body = e.text;
      size_t start = 0;
      while (start <= body.size()) {
        size_t end = body.find(';', start);
        std::string item = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t eq = item.find('=');
        std::string name = upper(item.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : hexDecode(item.substr(eq + 1));
        if (name == "544E") {
          if (value == "xterm" || value == "xterm-256color") tn_name_ = "xterm";
          else if (value == "xterm-kitty") tn_name_ = "kitty";
          else if (value == "mlterm") tn_name_ = "mlterm";
        } else if (name == "524742") {
          tcap_rgb_ = true;
        } else if (name == "687061") {
          tcap_hpa_ = true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
      }
      break;
    }
    case ReplyKind::SECONDARY_DA:
      da2_pp_ = param(0);
      da2_pv_ = param(1);
      break;
    case ReplyKind::DEFAULT_COLOR:
      if (e.params.size() == 4) {
        Color c(e.params[1], e.params[2], e.params[3]);
        if (e.params[0] == 10) default_fg_ = c;
        else default_bg_ = c;
      }
      break;
    case ReplyKind::KITTY_KEYBOARD:
      kitty_keyboard_ = true;
      break;
    case ReplyKind::MODE_REPORT: {
      bool set = param(1) >= 1 && param(1) <= 3;
      if (param(0) == 2026) sync_ = set;
      else if (param(0) == 1016) pixel_mouse_ = set;
      break;
    }
    case ReplyKind::XTSMGRAPHICS:
      if (param(1) != 0) break;
      if (param(0) == 1 && param(2) > 0) sixel_registers_ = param(2);
      else if (param(0) == 2 && param(3) > 0) {
        sixel_max_w_ = param(2);
        sixel_max_h_ = param(3);
      }
      break;
    case ReplyKind::KITTY_GRAPHICS: {
      size_t semi = e.text.find(';');
      if (semi != std::string::npos && e.text.substr(semi + 1) == "OK") kitty_graphics_ = true;
      break;
    }
    case ReplyKind::WINDOW_REPORT:
      if (param(0) == 4) {
        text_px_h_ = param(1);
        text_px_w_ = param(2);
      } else if (param(0) == 6) {
        cell_px_h_ = param(1);
        cell_px_w_ = param(2);
      } else if (param(0) == 8) {
        rows_ = param(1);
        cols_ = param(2);
      }
      break;
    case ReplyKind::ITERM_CELL_SIZE:
      cell_px_h_ = param(0);
      cell_px_w_ = param(1);
      break;
    case ReplyKind::PRIMARY_DA:
      complete_ = true;
      da1_sixel_ = std::find(e.params.begin() + (e.params.empty() ? 0 : 1), e.params.end(), 4) != e.params.end();
      break;
    default:
      break;
  }
}

Capabilities Discovery::result() const {
  Capabilities caps;
  caps.answered = complete_;

  // Identity: the platform first, then the unit id, the version reply, the
  // terminal-name capability and the secondary attributes, in that order.
  std::string name;
  if (env_.linux_console) name = "linux";
  else if (startsWith(env_.term, "rxvt")) name = "rxvt";
  else if (env_.term_program == "Apple_Terminal") name = "apple";
  else if (env_.windows) name = "windows";
  else if (!da3_name_.empty()) name = da3_name_;
  else if (!xtversion_name_.empty()) name = xtversion_name_;
  else if (!tn_name_.empty()) name = tn_name_;
  else if (da2_pp_ == 83) name = "screen";
  else if (contains(env_.term, "alacritty") && da2_pp_ >= 0) name = "alacritty";
  else if (env_.term_program == "iTerm.app") name = "iterm2";
  else if (env_.konsole) name = "konsole";
  std::string version;
  if (!xtversion_name_.empty() && xtversion_name_ == name) version = xtversion_version_;
  else if (da2_pv_ >= 0 && (name == "screen" || name == "alacritty" || name == "xterm")) {
    if (name == "xterm") version = std::to_string(da2_pv_);
    else version = std::to_string(da2_pv_ / 10000) + "." + std::to_string(da2_pv_ / 100 % 100) + "." + std::to_string(da2_pv_ % 100);
  }
  caps.terminal = name.empty() ? env_.term : name;
  caps.version = version;
  caps.rxvt = name == "rxvt";

  // Colour depth.
  int term_colors = colorsForTerm(env_.term);
  bool always_direct = name == "kitty" || name == "alacritty" || name == "foot" || name == "wezterm" ||
                       name == "contour" || name == "iterm2" || name == "konsole" || name == "windows";
  // Terminals known by $TERM_PROGRAM to take 24-bit colour even when
  // $COLORTERM does not say so.
  bool program_direct = env_.term_program == "iTerm.app" || env_.term_program == "Hyper" ||
                        env_.term_program == "HyperTerm" || env_.term_program == "MacTerm";
  bool truecolor = term_colors > 256 || env_.colorterm == "truecolor" || env_.colorterm == "24bit" || tcap_rgb_ ||
                   always_direct || program_direct;
  if (name == "screen" && !atLeast(version, "5")) truecolor = false;
  if (name == "apple" || name == "terminology") truecolor = false;
  if (truecolor) caps.colors = ColorDepth::TRUECOLOR;
  else if (term_colors >= 256) caps.colors = ColorDepth::C256;
  else if (term_colors >= 8) caps.colors = ColorDepth::C8;
  else caps.colors = ColorDepth::NONE;
  if (env_.colors_override == "none") caps.colors = ColorDepth::NONE;
  else if (env_.colors_override == "8") caps.colors = ColorDepth::C8;
  else if (env_.colors_override == "256") caps.colors = ColorDepth::C256;
  else if (env_.colors_override == "24bit") caps.colors = ColorDepth::TRUECOLOR;

  caps.column_move = term_colors > 0 || tcap_hpa_ || complete_;
  caps.display.default_fg = default_fg_;
  caps.display.default_bg = default_bg_;
  // A terminal that reports only its background most likely draws text in
  // whichever of black and white stands out against it.
  caps.appearance = detectAppearance(default_fg_, default_bg_);
  if (!default_fg_ && default_bg_) {
    caps.display.default_fg = caps.appearance == Appearance::LIGHT ? Color(0, 0, 0) : Color(255, 255, 255);
  }
  caps.kitty_keyboard = kitty_keyboard_;
  caps.pixel_mouse = pixel_mouse_;
  caps.display.key_release = kitty_keyboard_;
  caps.display.precise_mouse = pixel_mouse_;
  caps.display.hover = true;

  // Synchronized output: what the mode query says, except on kitty (long
  // updates with large uploads went wrong there); some terminals implement
  // the DCS form without answering the query.
  if (sync_ && name != "kitty") caps.sync = SyncForm::MODE;
  else if (name == "alacritty" || name == "iterm2") caps.sync = SyncForm::DCS;

  // Glyph repertoire.
  caps.display.sextants = name == "kitty" || name == "vte" || name == "foot" || name == "contour" ||
                  (name == "wezterm" && version >= "20210610");
  caps.quadrants = caps.display.sextants || name == "wezterm" || name == "alacritty" || name == "iterm2" ||
                   name == "konsole" || name == "mlterm" || name == "terminology" || name == "rxvt" || name == "windows";
  caps.braille = name != "rxvt" && name != "linux";
  caps.half_blocks = name != "linux";
  caps.undercurl = name == "kitty" || name == "vte" || name == "mintty" || (name == "wezterm" && version >= "20210610");
  caps.distinct_default_bg = name == "kitty";

  // Cell size: the cell report, else the text area divided by the size,
  // else the kernel's window size.
  int rows = rows_, cols = cols_;
  caps.rows = rows;
  caps.cols = cols;
  if (cell_px_h_ > 0 && cell_px_w_ > 0) {
    caps.display.cell_px_height = cell_px_h_;
    caps.display.cell_px_width = cell_px_w_;
  } else if (text_px_h_ > 0 && text_px_w_ > 0 && rows > 0 && cols > 0) {
    caps.display.cell_px_height = text_px_h_ / rows;
    caps.display.cell_px_width = text_px_w_ / cols;
  } else if (win_cell_px_h_ > 0 && win_cell_px_w_ > 0) {
    caps.display.cell_px_height = win_cell_px_h_;
    caps.display.cell_px_width = win_cell_px_w_;
  }

  caps.display.display_scale = estimateDisplayScale(caps.display.cell_px_height);

  // Images: kitty graphics, then iTerm2's, then Sixel.
  bool sixel = da1_sixel_ || (name == "alacritty" && sixel_max_w_ > 0);
  if (sixel) {
    caps.sixel_registers = sixel_registers_ > 0 ? std::min(sixel_registers_, 65534) : 256;
    caps.sixel_max_width = sixel_max_w_;
    caps.sixel_max_height = sixel_max_h_;
    std::string decsdm;
    if (name == "xterm") decsdm = atLeast(version, "369") ? "\x1b[?80l" : "\x1b[?80h";
    else if (name == "foot") decsdm = atLeast(version, "1.8.2") ? "\x1b[?80l" : "\x1b[?80h";
    else if (name == "alacritty") decsdm = atLeast(version, "0.15.1") ? "\x1b[?80l" : "\x1b[?80h";
    else if (name == "contour") decsdm = "\x1b[?80h";
    else if (name == "mintty" && !atLeast(version, "3.5.2")) decsdm = "\x1b[?80h";
    caps.sixel_setup = decsdm + "\x1b[?8452h";
  }
  if (kitty_graphics_) caps.pixels = PixelProtocol::KITTY;
  else if (name == "iterm2") caps.pixels = PixelProtocol::ITERM2;
  else if (sixel) caps.pixels = PixelProtocol::SIXEL;
  caps.kitty_graphics_animation = kitty_graphics_ && name == "kitty" && atLeast(version, "0.20");
  if (env_.pixels_override == "none") caps.pixels = PixelProtocol::NONE;
  else if (env_.pixels_override == "sixel") caps.pixels = PixelProtocol::SIXEL;
  else if (env_.pixels_override == "kitty") caps.pixels = PixelProtocol::KITTY;
  else if (env_.pixels_override == "iterm2") caps.pixels = PixelProtocol::ITERM2;
  if (caps.display.cell_px_height <= 0 || caps.display.cell_px_width <= 0) caps.pixels = PixelProtocol::NONE;
  caps.display.pictures = caps.pixels != PixelProtocol::NONE;
  return caps;
}

}  // namespace cellui::term
