#include "TerminalUI.h"

#include "InputEvent.h"
#include "Controller.h"
#include "UIMenu.h"
#include "Chart.h"
#include "HeatmapChart.h"
#include "dsp/DiracAnalyzer.h"
#include "AudioAPI.h"
#include "LaunchpadIO.h"
#include "LaunchpadPadEvent.h"

#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cassert>
#include <unistd.h>
#include <memory>
#include <cmath>
#include <algorithm>
#include <fmt/core.h>

#include <sys/time.h>

#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Plot.hh>
#include <ncpp/Reader.hh>
#include <ncpp/Menu.hh>
#include <ncpp/Selector.hh>
#include <ncpp/Visual.hh>

#include <poll.h>

using namespace ncpp;
using namespace std;
using namespace fmt;

static inline ncinput to_ncinput(const InputEvent & input) {
  ncinput ni = { .id = input.getId(), .y = input.getY(), .x = input.getX(), .utf8 = { 0, 0, 0, 0, 0 }, .alt = input.hasAlt(), .shift = input.hasShift(), .ctrl = input.hasCtrl(), .evtype = NCTYPE_UNKNOWN, .modifiers = ((input.hasAlt() ? NCKEY_MOD_ALT : 0) | (input.hasCtrl() ? NCKEY_MOD_CTRL : 0) | (input.hasShift() ? NCKEY_MOD_SHIFT : 0) | (input.hasMeta() ? NCKEY_MOD_META : 0)), .ypx = -1, .xpx = -1 };
  return ni;
}

static inline long long now() {
  struct timeval tv;
  int r = gettimeofday(&tv, 0);
  if (r == 0) {
    return (long long)1000 * tv.tv_sec + tv.tv_usec / 1000;
  } else {
    return 0;
  }
}

class TerminalPlane : public UIPlane {
public:
  TerminalPlane(std::shared_ptr<Controller> & _controller, Plane * _plane, bool _owner = true) : UIPlane(_controller), plane(_plane), owner(_owner) {
    unsigned int y, x;
    plane->get_dim(&y, &x);
    setDim(pair(static_cast<int>(y), static_cast<int>(x)));
    setPosition(pair(0, 0));
  }
  ~TerminalPlane() {
    if (owner) delete plane;
  }
  void resize(int rows, int cols) override {
    if (plane->to_ncplane()) {
      UIPlane::resize(rows, cols);
      plane->resize(rows, cols);
    }
  }
  void move(int y, int x) override {
    if (plane->to_ncplane()) {
      UIPlane::move(y, x);
      plane->move(y, x);
    }
  }
  void setFgColor(int r, int g, int b) override { plane->set_fg_rgb8(r, g, b); }
  void setBgColor(int r, int g, int b) override { plane->set_bg_rgb8(r, g, b); }
  void setUnderline(bool b) override {
    if (b) {
      plane->styles_set(CellStyle::Underline);
    } else {
      plane->styles_set(CellStyle::None);
    }
  }
  void erase() override { plane->erase(); }
  void putstr(int y, int x, const std::string & s) override { plane->putstr(y, x, s.c_str()); }
  unique_ptr<UIPlane> createChild() override {
    auto plane = new Plane(1, 1, 0, 0);
    plane->set_base("", 0, NCCHANNELS_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
    // plane->set_scrolling(true);
    // plane->rounded_box(NCSTYLE_NONE, NCCHANNELS_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20), 0, 0, 0);
    // plane->putstr("");
    return make_unique<TerminalPlane>(getController(), plane);
  }
  
  void drawBorder() override {
    plane->erase();

    unsigned fg_red, fg_green, fg_blue;
    plane->get_fg_rgb8(&fg_red, &fg_green, &fg_blue);

    unsigned bg_red, bg_green, bg_blue;
    plane->get_bg_rgb8(&bg_red, &bg_green, &bg_blue);

    auto channels = NCCHANNELS_INITIALIZER(fg_red, fg_green, fg_blue, bg_red, bg_green, bg_blue);
    
    nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
    nccell lr = NCCELL_TRIVIAL_INITIALIZER, ll = NCCELL_TRIVIAL_INITIALIZER;
    nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;
    if (nccells_rounded_box(plane->to_ncplane(), NCSTYLE_NONE, 0, &ul, &ur, &ll, &lr, &hl, &vl)) {
      return;
    }
    ul.channels = ur.channels = ll.channels = lr.channels = hl.channels = vl.channels = channels;
    nccell_set_bg_alpha(&ul, NCALPHA_BLEND);
    nccell_set_bg_alpha(&ur, NCALPHA_BLEND);
    nccell_set_bg_alpha(&ll, NCALPHA_BLEND);
    nccell_set_bg_alpha(&lr, NCALPHA_BLEND);
    nccell_set_bg_alpha(&hl, NCALPHA_BLEND);
    nccell_set_bg_alpha(&vl, NCALPHA_BLEND);
    
    if (ncplane_perimeter(plane->to_ncplane(), &ul, &ur, &ll, &lr, &hl, &vl, 0)) {
      nccell_release(plane->to_ncplane(), &ul); nccell_release(plane->to_ncplane(), &ur); nccell_release(plane->to_ncplane(), &hl);
      nccell_release(plane->to_ncplane(), &ll); nccell_release(plane->to_ncplane(), &lr); nccell_release(plane->to_ncplane(), &vl);
      return;
    }
    nccell_release(plane->to_ncplane(), &ul); nccell_release(plane->to_ncplane(), &ur); nccell_release(plane->to_ncplane(), &hl);
    nccell_release(plane->to_ncplane(), &ll); nccell_release(plane->to_ncplane(), &lr); nccell_release(plane->to_ncplane(), &vl);
  }
  
  // y/x/rows/cols let a caller that isn't a one-line plane (PatternEditor's
  // annotation editing, which needs the reader positioned at the cursor's
  // own screen row, not row 0) place and size the reader plane explicitly.
  // x == -1/rows == -1/cols == -1 (the defaults) reproduce exactly what
  // this used to hardcode - StatusLine's existing calls are untouched.
  void showReader(const std::string & prompt = "", int y = 0, int x = -1, int rows = -1, int cols = -1,
		   const std::string & initial_text = "") override {
    if (!readerActive()) {
      setOwning(false);

      // The reader plane below is opaque and covers its own bounds, so any
      // prompt text must be drawn onto *this* (the still-visible underlying
      // plane) first, and the reader plane offset past it - otherwise the
      // prompt is drawn then immediately hidden under the reader, and the
      // whole M-x minibuffer silently looks like it never opened even
      // though it's actually active and correctly accepting input.
      if (!prompt.empty()) putstr(y, 0, prompt);
      auto prompt_width = static_cast<unsigned int>(prompt.size());

      ncreader_options reader_opts;
      reader_opts.tchannels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0x00, 0x00, 0x00);
      ncchannels_set_fg_alpha(&reader_opts.tchannels, NCALPHA_HIGHCONTRAST);
      ncchannels_set_bg_alpha(&reader_opts.tchannels, NCALPHA_BLEND);
      reader_opts.tattrword = 0; // attributes used for input
      reader_opts.flags = NCREADER_OPTION_CURSOR | NCREADER_OPTION_HORSCROLL;

      auto [plane_rows, plane_cols] = getDim();
      auto reader_x = x == -1 ? static_cast<int>(prompt_width) : x;
      auto reader_rows = rows == -1 ? static_cast<unsigned int>(plane_rows) : static_cast<unsigned int>(rows);
      unsigned int reader_cols;
      if (cols == -1) {
	reader_cols = static_cast<unsigned int>(plane_cols) > prompt_width ?
	  static_cast<unsigned int>(plane_cols) - prompt_width : 1u;
      } else {
	reader_cols = cols > 0 ? static_cast<unsigned int>(cols) : 1u;
      }

      ncplane_options opts = {
	.y = y,
	.x = reader_x,
	.rows = reader_rows,
	.cols = reader_cols,
	.userptr = nullptr,
	.name = nullptr,
	.resizecb = nullptr,
	.flags = 0,
	.margin_b = 0,
	.margin_r = 0
      };

      auto reader_plane = ncplane_create(getPlane().to_ncplane(), &opts);
      ncplane_set_fg_rgb8(reader_plane, 0x80, 0xc0, 0x80);
      ncplane_set_bg_rgb8(reader_plane, 0x00, 0x40, 0x00);
      ncplane_set_base(reader_plane, "", 0, 0);
      reader = ncreader_create(reader_plane, &reader_opts);

      // ncreader has no "set full contents" call - only single-EGC writes -
      // so seed it by feeding initial_text's own UTF-8 codepoints one at a
      // time. Splitting on codepoint boundaries rather than full Unicode
      // grapheme clusters (combining marks/ZWJ sequences would each become
      // a separate write instead of one grouped EGC) is an acceptable
      // first-pass approximation for plain annotation text - see
      // PatternEditor's own annotation-editing entry points.
      size_t i = 0;
      while (i < initial_text.size()) {
	unsigned char c = static_cast<unsigned char>(initial_text[i]);
	size_t len = 1;
	if ((c & 0xE0) == 0xC0) len = 2;
	else if ((c & 0xF0) == 0xE0) len = 3;
	else if ((c & 0xF8) == 0xF0) len = 4;
	len = std::min(len, initial_text.size() - i);
	ncreader_write_egc(reader, initial_text.substr(i, len).c_str());
	i += len;
      }
    }
  }

  bool readerActive() const override { return reader != 0; }

  string closeReader() override {
    char* contents;
    ncreader_destroy(reader, &contents);
    string r = contents;
    free(contents);    
    reader = 0;
    return r;
  }

  void showPicker() override {
    ncselector_options opts =
      {
       .title = nullptr,
       .secondary = nullptr,
       .footer = nullptr,
       .defidx = 0,
       .maxdisplay = 0,
       .opchannels = 0,
       .descchannels = 0,
       .titlechannels = 0,
       .footchannels = 0,
       .boxchannels = 0,
       .flags = 0
      };
    selector = make_unique<Selector>(getPlane(), &opts);
  }

  void addItem(const string & id, const string & label) override {
    if (selector) {
      char * option = new char[id.size() + 1];
      char * desc = new char[label.size() + 1];
      
      strcpy(option, id.c_str());
      strcpy(desc, label.c_str());
      
      ncselector_item item =
	{
	 .option = option,
	 .desc = desc
	};
      selector->additem(&item);
    }
  }

  void clearItems() override {

  }

  bool offerInput(const InputEvent & input) override {
    if (reader) {
      auto ni = to_ncinput(input);
      ncreader_offer_input(reader, &ni);
      return true;
    } else if (selector) {
      auto ni = to_ncinput(input);
      return selector->offer_input(&ni);
    } else {
      return false;
    }
  }
  
  void setOwning(bool t) { owner = t; }

  Plane & getPlane() { return *plane; }

  void refresh() override {
    unsigned int y, x;
    plane->get_dim(&y, &x);
    setDim(pair(static_cast<int>(y), static_cast<int>(x)));    
  }

private:
  Plane * plane;
  ncreader * reader = 0;
  unique_ptr<Selector> selector;
  bool owner;
};

class TerminalMenu : public UIMenu {
public:
  TerminalMenu() {
    ncmenu_item file_items[] = { { .desc = "New", .shortcut = { .id = 'N', .ctrl = true } },
				 // { .desc = "Open", .shortcut = { .id = 'O' } },
				 // { .desc = "Quit", .shortcut = { .id = 'q' } }
    };
    
    ncmenu_section sections[] = { { .name = "File", .itemcount = 1, .items = file_items, .shortcut = { .id = 'f', .alt = true } }
    };
    uint64_t headerchannels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0x7f, 0x34, 0x7f);
    uint64_t sectionchannels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0x00, 0x00, 0x00);
    ncchannels_set_fg_alpha(&sectionchannels, NCALPHA_HIGHCONTRAST);
    ncchannels_set_bg_alpha(&sectionchannels, NCALPHA_BLEND);
    ncchannels_set_bg_alpha(&headerchannels, NCALPHA_BLEND);
    ncmenu_options mopts = { .sections = sections, .sectioncount = 1, .headerchannels = headerchannels, .sectionchannels = sectionchannels, .flags = 0 };
    menu = make_unique<Menu>(&mopts);
  }

  bool offerInput(const InputEvent & input) override {
    auto ni = to_ncinput(input);
    auto r = menu->offer_input(&ni);
    auto s = menu->get_selected();
    menu_selected = s ? s : "";
    return r;
  }

  std::string getSelected() const { return menu_selected; }
  
private:
  unique_ptr<Menu> menu;
  string menu_selected;
};

class TerminalChart : public Chart {
public:
  TerminalChart(UIPlane & parent, ChartType type, double min_y = 0.0, double max_y = 0.0) : Chart(parent, type, min_y, max_y) {
    // Same window_bg_color base-cell fix as plot_plane_ gets below, but
    // for this widget's own outer plane - the footer label row is
    // deliberately left uncovered by plot_plane_ so its text shows
    // through, and any of that row's cells the label text itself doesn't
    // reach (getMeterLabel() rarely fills the whole width) need this too.
    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    uint64_t base_channels = NCCHANNELS_INITIALIZER(21, 21, 21, 21, 21, 21);
    ncplane_set_base(tplane.getPlane().to_ncplane(), " ", 0, base_channels);
  }

  void setSample(int i, double v) override {
    if (!plot_) {
      // ncdplot_create()/ncdplot_destroy() take ownership of the ncplane
      // passed in and destroy it together with the plot (confirmed
      // empirically: resizing the plane after destroying the plot
      // segfaults). Since this chart's own plane (getPlane()) must survive
      // resizes for the chart's whole lifetime, give the plot a dedicated,
      // disposable child plane instead of handing away our own.
      auto [rows, cols] = getDim();
      auto [y, x] = getPosition();
      plot_plane_ = getPlane().createChild();
      // createChild()'s underlying Plane ctor has no parent-plane argument -
      // it places the new plane at (0,0) in the standard plane's coordinate
      // space, not relative to our own (already correctly positioned)
      // plane. Reposition it explicitly to match, or it always ends up at
      // whatever raw (0,0) createChild() hardcodes regardless of where this
      // chart actually is on screen.
      // When a footer label is set, the plot only gets rows-1 - it's a
      // child plane, so leaving the last row of our own (outer) plane_
      // uncovered is what lets that row's putstr() in commit() actually
      // show through, rather than being hidden behind the plot child.
      int plot_rows = footer_label_.empty() ? rows : rows - 1;
      if (plot_rows <= 0) plot_rows = rows;
      plot_plane_->resize(plot_rows, cols);
      plot_plane_->move(y, x);

      auto & tplane = dynamic_cast<TerminalPlane&>(*plot_plane_);
      tplane.setOwning(false);

      // Set the plane's *base* cell to the same idle background every
      // scope now shares (StyleProvider::window_bg_color "#151515",
      // HeatmapChart's own kHeatmapBackground) - this, not a plain
      // putstr()-based fill (tried and reverted here), is what actually
      // survives ncdplot's own rendering: ncdplot_create()/every
      // subsequent redraw calls ncplane_erase() internally on its own
      // plane before repainting the "lit" portion of each column, and
      // per notcurses's own contract "the base cell is not affected by
      // ncplane_erase()" - it's what shows through any cell whose real
      // content is still blank (gcluster 0) after that erase. A plain
      // fill got wiped by that same erase every render, leaving a freshly
      // created plane's own default (the raw terminal's background, not
      // this app's) showing through the "no data" portion of the chart -
      // a real, confirmed bug.
      uint64_t base_channels = NCCHANNELS_INITIALIZER(21, 21, 21, 21, 21, 21);
      ncplane_set_base(tplane.getPlane().to_ncplane(), " ", 0, base_channels);

      ncplot_options opts;
      memset(&opts, 0, sizeof(opts));
      opts.flags = 0
	// | NCPLOT_OPTION_LABELTICKSD
	// | NCPLOT_OPTION_EXPONENTIALD
	// | NCPLOT_OPTION_PRINTSAMPLE
	;
      opts.gridtype = getType() == DOTS ? NCBLIT_BRAILLE : NCBLIT_2x2;
      // opts.gridtype = NCBLIT_8x1;

      // Opaque window_bg_color ("#151515") background, not NCALPHA_BLEND -
      // blend mode composites against whatever's on the plane beneath at
      // render time, which for a cell ncdplot actually writes to isn't
      // reliably this app's own background (a real, confirmed bug: every
      // rendered dot showed the raw terminal's own background bleeding
      // through, even though ncplane_set_base() above already fixed the
      // *untouched* cells around them). Foreground gradient (dot color)
      // is unchanged - only the background channel/alpha needed fixing.
      opts.minchannels = NCCHANNELS_INITIALIZER(0x80, 0x80, 0xff, 21, 21, 21);
      opts.maxchannels = NCCHANNELS_INITIALIZER(0x80, 0xff, 0x80, 21, 21, 21);

      plot_ = std::make_shared<PlotD>(tplane.getPlane(), &opts);
    }

    plot_->set_sample(i, v);
  }

  void commit() override {
    if (!footer_label_.empty()) {
      auto [rows, cols] = getDim();
      // window_fg_color/window_bg_color ("#9e9e9e"/"#151515") - this
      // widget has no StyleProvider reference of its own (only
      // UI-level/render-time callers normally do), so these are the same
      // literal RGB values kHeatmapBackground uses above for the same
      // reason. Without this, the label text drew with whatever fg/bg the
      // outer plane's draw state last happened to be left in - unset in
      // practice, showing the raw terminal's own background instead of
      // this app's (the outer-plane counterpart of the ncplot fix above -
      // that one only covers plot_plane_, not this row, which is
      // deliberately left uncovered by it so this text shows through).
      setFgColor(0x9e, 0x9e, 0x9e);
      setBgColor(21, 21, 21);
      putstr(rows - 1, 0, footer_label_);
    }
  }

protected:
  void onResize() override {
    plot_.reset();       // destroys the ncdplot, which destroys plot_plane_'s ncplane too
    plot_plane_.reset();  // drop our now-hollow wrapper (owner=false, so no double-free)
    // next setSample() lazily rebuilds both against the new dimensions
  }

private:
  std::shared_ptr<PlotD> plot_;
  std::unique_ptr<UIPlane> plot_plane_;
};

// Renders via notcurses's ncvisual/pixel-graphics subsystem (sixel/kitty-
// graphics/iTerm2, whichever the terminal supports) instead of ncplot's
// braille/block glyphs, for much higher effective resolution. Buffers
// samples cheaply per setSample() call and does the actual RGBA-build-and-
// blit work once per commit(), directly onto this chart's own plane (no
// widget/plane-ownership landmine like TerminalChart's ncplot - ncvisual
// blitting draws onto an existing plane, it doesn't adopt/destroy it).
class TerminalPixelChart : public Chart {
public:
  TerminalPixelChart(UIPlane & parent, ChartType type, double min_y = 0.0, double max_y = 0.0) : Chart(parent, type, min_y, max_y) { }

  void setSample(int i, double v) override {
    if (i >= static_cast<int>(samples_.size())) samples_.resize(i + 1);
    samples_[i] = v;
  }

  void commit() override {
    if (samples_.empty()) return;

    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    auto native_plane = tplane.getPlane().to_ncplane();

    unsigned pxy = 0, pxx = 0, celldimy = 0;
    ncplane_pixel_geom(native_plane, &pxy, &pxx, &celldimy, nullptr, nullptr, nullptr);
    if (pxy == 0 || pxx == 0) return;

    // Reserve exactly one character row's worth of pixels at the bottom for
    // the footer label (see Chart::setFooterLabel), so the bar image itself
    // never gets drawn under/behind the text - pxy is always an exact
    // multiple of celldimy per ncplane_pixel_geom's own contract, so this
    // shrinks the image by exactly one whole cell row, not a partial one.
    if (!footer_label_.empty() && celldimy > 0 && pxy > celldimy) pxy -= celldimy;

    // Opaque window_bg_color ("#151515"), not transparent (0 alpha) - a
    // transparent pixel here composites against the raw terminal's own
    // background instead of this app's, since pixel-graphics blitting
    // replaces a cell's usual text-mode background entirely rather than
    // layering over whatever this plane's cells were otherwise painted
    // (a real, confirmed bug: this chart's empty area showed Ubuntu's
    // default terminal color instead of window_bg_color).
    vector<uint32_t> buffer(static_cast<size_t>(pxy) * pxx, 0xff151515u);

    auto range = max_y_ - min_y_;
    auto num_samples = samples_.size();
    for (unsigned x = 0; x < pxx; x++) {
      auto sample_idx = min(static_cast<size_t>(x) * num_samples / pxx, num_samples - 1);
      auto v = samples_[sample_idx];
      auto frac = range > 0 ? (v - min_y_) / range : 0.0;
      if (frac < 0) frac = 0;
      else if (frac > 1) frac = 1;
      auto bar_height = static_cast<unsigned>(frac * pxy);

      for (unsigned y = 0; y < bar_height; y++) {
	// dim blue-ish at the bottom (quiet) to green at the top (loud),
	// matching TerminalChart's existing min/max channel colors.
	double t = pxy > 1 ? static_cast<double>(y) / (pxy - 1) : 0.0;
	uint8_t r = static_cast<uint8_t>(0x80);
	uint8_t g = static_cast<uint8_t>(0x80 * (1 - t) + 0xff * t);
	uint8_t b = static_cast<uint8_t>(0xff * (1 - t) + 0x80 * t);
	unsigned py = pxy - 1 - y; // bars grow upward from the bottom
	buffer[py * pxx + x] = (0xffu << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
      }
    }

    ncpp::Visual visual(buffer.data(), static_cast<int>(pxy), static_cast<int>(pxx * 4), static_cast<int>(pxx));
    ncvisual_options vopts{};
    vopts.n = native_plane;
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;
    visual.blit(&vopts);

    if (!footer_label_.empty()) {
      auto [rows, cols] = getDim();
      putstr(rows - 1, 0, footer_label_);
    }
  }

private:
  std::vector<double> samples_;
};

namespace {
// Encodes a single codepoint as UTF-8 - up through 3-byte (BMP) covers
// space and the Block Elements quadrants; the 4-byte case is needed for
// TerminalHeatmapChart's sextant glyphs (Symbols for Legacy Computing,
// U+1FB00+), which sit past the BMP.
std::string utf8Encode(uint32_t codepoint) {
  std::string s;
  if (codepoint <= 0x7F) {
    s += static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    s += static_cast<char>(0xC0 | (codepoint >> 6));
    s += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    s += static_cast<char>(0xE0 | (codepoint >> 12));
    s += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (codepoint >> 18));
    s += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
  return s;
}

// Unicode Block Elements (U+2580-U+259F) quadrant glyphs, indexed by a
// 4-bit mask - bit0=upper-left, bit1=upper-right, bit2=lower-left,
// bit3=lower-right, 1 = the "on"/foreground group. Covers all 16 on/off
// patterns of a 2x2 sub-cell (mask 0, all-off, is plain space U+0020,
// outside the Block Elements range). The fallback (TerminalHeatmapChart)
// prefers the newer Unicode 13 sextant range (U+1FB00+, sextantCodepoint()
// below) when notcurses_cansextant() confirms the terminal actually
// supports it - real per-terminal font-coverage risk was the original
// reason sextants were skipped entirely, but querying the terminal
// directly (rather than guessing) resolves that; this quadrant table
// remains the fallback-of-the-fallback for terminals that report no
// sextant support.
constexpr uint32_t kQuadrantCodepoints[16] = {
  0x0020, 0x2598, 0x259D, 0x2580,
  0x2596, 0x258C, 0x259E, 0x259B,
  0x2597, 0x259A, 0x2590, 0x259C,
  0x2584, 0x2599, 0x259F, 0x2588,
};

// Unicode 13 sextant glyph (Symbols for Legacy Computing) for a 6-bit
// mask over a 2-column x 3-row sub-cell, bit weights top-left=1,
// top-right=2, mid-left=4, mid-right=8, bottom-left=16, bottom-right=32
// (i.e. bit = row*2+col, the same row-major convention kQuadrantCodepoints
// uses, just with 3 rows instead of 2), 1 = "on"/foreground. Of the 64
// patterns, 4 reuse pre-existing codepoints instead of the new block:
// mask 0 (all off) = space U+0020, mask 63 (all on) = full block U+2588,
// mask 21 (0b010101, left column only) = LEFT HALF BLOCK U+258C, mask 42
// (0b101010, right column only) = RIGHT HALF BLOCK U+2590. The remaining
// 60 masks get consecutive new codepoints U+1FB00..U+1FB3B in mask order,
// skipping 21/42.
uint32_t sextantCodepoint(int mask) {
  if (mask == 0) return 0x0020;
  if (mask == 63) return 0x2588;
  if (mask == 21) return 0x258C;
  if (mask == 42) return 0x2590;
  int index = 0;
  for (int m = 1; m < mask; m++) {
    if (m != 21 && m != 42) index++;
  }
  return 0x1FB00u + static_cast<uint32_t>(index);
}

struct HeatmapRgb { float r, g, b; };

// StyleProvider::window_bg_color ("#151515") - the same idle background
// every scope now shares (the FFT/volume-meter charts already show it
// through untouched cells; the heatmap has no such "untouched" concept
// since it repaints every cell/pixel every frame, so it needs its own
// explicit idle color instead of literal black to match).
constexpr HeatmapRgb kHeatmapBackground{21.0f, 21.0f, 21.0f};

// Lerps from kHeatmapBackground (value == 0) to the fully-bright HSV color
// (value == 1), using value itself as the blend weight - continuous by
// construction, so a cell whose value only ever asymptotically approaches
// 0 (dsp/DiracAnalyzer.cpp's grid ballistics are a one-pole decay that
// never mathematically reaches exact 0) still converges to the background
// color rather than needing a separate cutoff/threshold to special-case
// "close enough to silent." No explicit epsilon check needed anywhere:
// once value is astronomically small, its weight in the lerp is too.
HeatmapRgb heatmapCellColor(float saturation, float value) {
  if (value < 0.0f) value = 0.0f;
  else if (value > 1.0f) value = 1.0f;
  uint8_t r, g, b;
  heatmapHsvToRgb(kHeatmapHue, saturation, 1.0f, r, g, b);
  return {
    kHeatmapBackground.r * (1.0f - value) + static_cast<float>(r) * value,
    kHeatmapBackground.g * (1.0f - value) + static_cast<float>(g) * value,
    kHeatmapBackground.b * (1.0f - value) + static_cast<float>(b) * value,
  };
}

// Exact (brute-force) optimal 2-color quantization of samples.size() (here
// always 4 - a quadrant's sub-cells) RGB samples: tries every non-trivial
// way to split them into an "on"/"off" group and keeps the split
// minimizing total squared color error against each group's own mean -
// cheap at this size (at most 16 candidate splits) and, unlike a fixed
// brightness threshold, actually accounts for hue/saturation variation
// too, not just brightness. Returns the winning bitmask (bit i set =
// sample i is in the "on"/foreground group) and writes the two
// group-mean colors out.
int quantizeToTwoColors(const std::vector<HeatmapRgb> & samples, HeatmapRgb & on_color, HeatmapRgb & off_color) {
  int n = static_cast<int>(samples.size());
  int best_mask = 0;
  float best_cost = -1.0f;
  HeatmapRgb best_on{}, best_off{};

  for (int mask = 0; mask < (1 << n); mask++) {
    HeatmapRgb sum_on{0, 0, 0}, sum_off{0, 0, 0};
    int count_on = 0, count_off = 0;
    for (int i = 0; i < n; i++) {
      if (mask & (1 << i)) { sum_on.r += samples[static_cast<size_t>(i)].r; sum_on.g += samples[static_cast<size_t>(i)].g; sum_on.b += samples[static_cast<size_t>(i)].b; count_on++; }
      else { sum_off.r += samples[static_cast<size_t>(i)].r; sum_off.g += samples[static_cast<size_t>(i)].g; sum_off.b += samples[static_cast<size_t>(i)].b; count_off++; }
    }
    HeatmapRgb mean_on = count_on > 0 ? HeatmapRgb{sum_on.r / count_on, sum_on.g / count_on, sum_on.b / count_on} : HeatmapRgb{0, 0, 0};
    HeatmapRgb mean_off = count_off > 0 ? HeatmapRgb{sum_off.r / count_off, sum_off.g / count_off, sum_off.b / count_off} : HeatmapRgb{0, 0, 0};

    float cost = 0.0f;
    for (int i = 0; i < n; i++) {
      auto & mean = (mask & (1 << i)) ? mean_on : mean_off;
      float dr = samples[static_cast<size_t>(i)].r - mean.r, dg = samples[static_cast<size_t>(i)].g - mean.g, db = samples[static_cast<size_t>(i)].b - mean.b;
      cost += dr * dr + dg * dg + db * db;
    }

    if (best_cost < 0.0f || cost < best_cost) {
      best_cost = cost;
      best_mask = mask;
      best_on = mean_on;
      best_off = mean_off;
    }
  }

  on_color = best_on;
  off_color = best_off;
  return best_mask;
}

// Computes destination index d's source-index range [lo,hi) along one axis
// of size grid_size, resampled to dest_size - two different, deliberately
// separate strategies depending on direction, because a single formula
// that's correct for one gets the other wrong:
//
// - Downsampling (dest_size <= grid_size, TerminalHeatmapChart's usual
//   case): a proper partition via round(d*grid_size/dest_size) boundaries,
//   not floor or ceiling. lo(d) is literally hi(d-1), so consecutive
//   buckets never leave a gap (a truncating/floor upper bound can drop a
//   source cell a bucket's real span only partially reaches into - a real,
//   confirmed bug: grid_size=18, dest_size=15, d=7's real span [8.4,9.6)
//   genuinely overlaps both source rows 8 and 9, but floor(9.6)=9 as an
//   exclusive bound only ever included row 8) nor overlap (a ceiling upper
//   bound independently applied to every d, tried and reverted here, fixed
//   the gap but then had d=6's range reach into row 8 too, so an
//   elevation=0 source split 50/50 across rows 8/9 by DiracAnalyzer.cpp's
//   own bilinear splat lit up all 3 sextant sub-rows instead of
//   concentrating in the true middle one).
//
// - Upsampling (dest_size > grid_size, TerminalPixelHeatmapChart's usual
//   case, and TerminalHeatmapChart's own azimuth axis before
//   DiracAnalyzer::kAzimuthBins was widened enough to avoid it): the same
//   round-boundary formula can tie at consecutive d (two different d's
//   round to the same source index), leaving one of them an empty range;
//   patching that by grabbing the *next* source index (as the downsampling
//   fallback below does) breaks the tie asymmetrically, double-covering
//   one source cell while its symmetric neighbor only gets single
//   coverage - the horizontal counterpart of the elevation bug above (a
//   source split 50/50 across two adjacent azimuth bins rendered as three
//   equally-bright sub-columns instead of a symmetric two-and-two split).
//   Each destination index instead independently gets its own single
//   nearest source cell, by that destination cell's real-valued *center*
//   (d+0.5) rather than its edge - ties are then impossible since
//   consecutive d's centers can never round to the same source index the
//   way consecutive *boundaries* can.
void axisRange(int d, int grid_size, int dest_size, int & lo, int & hi) {
  if (dest_size <= grid_size) {
    lo = (d * grid_size + dest_size / 2) / dest_size;
    hi = ((d + 1) * grid_size + dest_size / 2) / dest_size;
    if (hi <= lo) hi = lo + 1; // degenerate (dest_size far smaller than grid_size): still show something
  } else {
    lo = ((2 * d + 1) * grid_size) / (2 * dest_size); // floor((d+0.5)*grid_size/dest_size)
    hi = lo + 1;
  }
  if (lo >= grid_size) lo = grid_size - 1;
  if (hi > grid_size) hi = grid_size;
}

// Resamples the logical grid_cols x grid_rows brightness/saturation grid
// to a dest_cols x dest_rows destination resolution, correct whether the
// destination is coarser (TerminalHeatmapChart's usual case, a handful of
// quadrant sub-cells) or finer (TerminalPixelHeatmapChart's usual case,
// real display pixels) than the logical grid: each destination cell
// aggregates (max, keeping saturation paired with whichever source cell
// "won" the max rather than maximized independently) over every source
// cell axisRange() (above) assigns it. Both dest_row 0 and grid row 0 are
// "bottom" here - callers flip to screen coordinates (row 0 = top)
// themselves.
void resampleGrid(const std::vector<float> & brightness, const std::vector<float> & saturation,
                   int grid_cols, int grid_rows, int dest_cols, int dest_rows,
                   std::vector<float> & out_brightness, std::vector<float> & out_saturation) {
  out_brightness.assign(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows), 0.0f);
  out_saturation.assign(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows), 0.0f);
  if (dest_cols <= 0 || dest_rows <= 0) return;

  for (int dy = 0; dy < dest_rows; dy++) {
    int gy0, gy1;
    axisRange(dy, grid_rows, dest_rows, gy0, gy1);
    for (int dx = 0; dx < dest_cols; dx++) {
      int gx0, gx1;
      axisRange(dx, grid_cols, dest_cols, gx0, gx1);

      float best_brightness = 0.0f, best_saturation = 0.0f;
      for (int gy = gy0; gy < gy1; gy++) {
        for (int gx = gx0; gx < gx1; gx++) {
          size_t src_idx = static_cast<size_t>(gy * grid_cols + gx);
          if (brightness[src_idx] > best_brightness) {
            best_brightness = brightness[src_idx];
            best_saturation = saturation[src_idx];
          }
        }
      }
      out_brightness[static_cast<size_t>(dy * dest_cols + dx)] = best_brightness;
      out_saturation[static_cast<size_t>(dy * dest_cols + dx)] = best_saturation;
    }
  }
}

struct AxisLabel { int row, col; const char * text; };

// The heatmap's own axis extremes, placed directly in the grid area rather
// than a separate legend row - elevation labels on the top/bottom rows
// (matching HeatmapChart's row-0-is-bottom-of-data/screen-row-0-is-top
// convention, so "+90" naturally sits where the top of the grid is),
// azimuth labels at the left/right edges of a middle row (azimuth bin 0
// is the left edge, bin kAzimuthBins-1 the right - see DiracAnalyzer.cpp's
// own az_pos splat math). Labels are always drawn, never hidden by
// activity underneath them - instead, each character cell a label
// occupies is an exception to the normal two-color quadrant/pixel
// blend: it collapses to a single mean color (its underlying sub-samples
// averaged) used as that character's background, with the glyph itself
// drawn in white blended with transparency over that background
// (labelForegroundColor(), below) - so the label always reads clearly
// while still visibly taking on whatever hue/brightness is really there.
constexpr float kLabelForegroundAlpha = 0.55f; // how much white shows through the glyph

void labelForegroundColor(const HeatmapRgb & bg, uint8_t & r, uint8_t & g, uint8_t & b) {
  r = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.r);
  g = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.g);
  b = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.b);
}

std::array<AxisLabel, 4> axisLabels(int usable_rows, int cols) {
  int el_col = max(0, (cols - 3) / 2); // "+90"/"-90" are both 3 characters wide
  return {{
    { 0, el_col, "+90" },
    { usable_rows - 1, el_col, "-90" },
    { usable_rows / 2, 0, "-180" },
    { usable_rows / 2, cols - 4, "+180" },
  }};
}
}

// Character-cell fallback for HeatmapChart, no pixel-graphics support
// needed: paints one Unicode block glyph per character cell - a sextant
// (2x3 sub-cells, sextantCodepoint() above) when notcurses_cansextant()
// confirms the terminal supports Unicode 13's sextant range, else a
// quadrant (2x2 sub-cells, kQuadrantCodepoints above), checked once at
// construction and cached (the terminal's capabilities don't change
// mid-session) - tripling (sextants) or doubling (quadrants) the
// effective vertical resolution over a flat one-color-per-cell approach.
// Each cell's foreground/background pair is the optimal 2-color
// quantization (quantizeToTwoColors) of its real sub-samples, not a naive
// corner pick or a fixed brightness threshold. 24-bit truecolor is
// near-universally supported even on terminals without sixel/Kitty image
// support, unlike the braille dot glyphs TerminalChart uses for 1D bars -
// a heatmap wants a genuine 2D color field, not a thresholded on/off
// pattern along one axis. Resamples (resampleGrid(), above) from the
// logical gridCols()xgridRows() grid to however many sub-cells this
// widget actually has (2x the character-cell count horizontally always,
// 3x or 2x vertically depending on sextant support).
class TerminalHeatmapChart : public HeatmapChart {
public:
  TerminalHeatmapChart(UIPlane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) {
    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    auto native_plane = tplane.getPlane().to_ncplane();
    use_sextants_ = notcurses_cansextant(ncplane_notcurses_const(native_plane));
  }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void commit() override {
    if (brightness_.empty()) return;
    auto [rows, cols] = getDim();
    int usable_rows = footer_label_.empty() ? rows : rows - 1;
    if (usable_rows <= 0 || cols <= 0) return;

    int sub_rows = use_sextants_ ? 3 : 2;
    int vcols = cols * 2, vrows = usable_rows * sub_rows; // sextant/quadrant sub-cell resolution
    std::vector<float> agg_brightness, agg_saturation;
    resampleGrid(brightness_, saturation_, gridCols(), gridRows(), vcols, vrows, agg_brightness, agg_saturation);

    std::vector<HeatmapRgb> samples(static_cast<size_t>(sub_rows * 2));
    for (int sy = 0; sy < usable_rows; sy++) {
      for (int sx = 0; sx < cols; sx++) {
        // Gather this character cell's sub-samples (row-major - top-left
        // first, matching kQuadrantCodepoints'/sextantCodepoint()'s own bit
        // order) from the resampled sub-grid. Screen row 0 is the TOP of
        // the widget, but sub-grid row 0 is the BOTTOM (resampleGrid()'s
        // contract, inherited from HeatmapChart.h's own setGrid()) - flip
        // vertically.
        int bit = 0;
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = sy * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            samples[static_cast<size_t>(bit)] = heatmapCellColor(agg_saturation[idx], agg_brightness[idx]);
            bit++;
          }
        }

        HeatmapRgb on_color, off_color;
        int mask = quantizeToTwoColors(samples, on_color, off_color);

        setFgColor(static_cast<int>(on_color.r), static_cast<int>(on_color.g), static_cast<int>(on_color.b));
        setBgColor(static_cast<int>(off_color.r), static_cast<int>(off_color.g), static_cast<int>(off_color.b));
        putstr(sy, sx, utf8Encode(use_sextants_ ? sextantCodepoint(mask) : kQuadrantCodepoints[mask]));
      }
    }
    setFgColor(255, 255, 255);
    setBgColor(static_cast<int>(kHeatmapBackground.r), static_cast<int>(kHeatmapBackground.g), static_cast<int>(kHeatmapBackground.b)); // don't leave the last cell's colors "stuck" for whatever draws next (e.g. the footer label below)

    for (auto & marker : markers_) {
      int sx = static_cast<int>(marker.u * static_cast<float>(cols));
      int sy = usable_rows - 1 - static_cast<int>(marker.v * static_cast<float>(usable_rows));
      if (sx < 0 || sx >= cols || sy < 0 || sy >= usable_rows) continue;
      putstr(sy, sx, '+');
    }

    for (auto & label : axisLabels(usable_rows, cols)) {
      std::string text = label.text;
      for (int c = 0; c < static_cast<int>(text.size()); c++) {
        int sx = label.col + c;
        if (sx < 0 || sx >= cols) continue;

        // This character's own sub-samples, collapsed to a single mean
        // color - the exception to the normal two-color sextant/quadrant
        // blend that a label-covered cell gets (see axisLabels()'s own
        // comment above).
        HeatmapRgb sum{0, 0, 0};
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = label.row * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            HeatmapRgb rgb = heatmapCellColor(agg_saturation[idx], agg_brightness[idx]);
            sum.r += rgb.r; sum.g += rgb.g; sum.b += rgb.b;
          }
        }
        float sample_count = static_cast<float>(sub_rows * 2);
        HeatmapRgb mean{sum.r / sample_count, sum.g / sample_count, sum.b / sample_count};

        uint8_t fr, fg, fb;
        labelForegroundColor(mean, fr, fg, fb);
        setFgColor(fr, fg, fb);
        setBgColor(static_cast<int>(mean.r), static_cast<int>(mean.g), static_cast<int>(mean.b));
        putstr(label.row, sx, std::string(1, text[static_cast<size_t>(c)]));
      }
    }

    if (!footer_label_.empty()) {
      putstr(rows - 1, 0, footer_label_);
    }
  }

private:
  bool use_sextants_;
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
};

// Pixel-graphics HeatmapChart renderer - same ncvisual/pixel-blit approach
// as TerminalPixelChart, but a genuine 2D image resampled (resampleGrid(),
// above - typically upscaling here, since real pixel counts usually exceed
// the logical grid's 36x18) to the plane's real pixel dimensions, instead
// of vertical bars.
class TerminalPixelHeatmapChart : public HeatmapChart {
public:
  TerminalPixelHeatmapChart(UIPlane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) { }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void commit() override {
    if (brightness_.empty()) return;

    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    auto native_plane = tplane.getPlane().to_ncplane();

    unsigned pxy = 0, pxx = 0, celldimy = 0, celldimx = 0;
    ncplane_pixel_geom(native_plane, &pxy, &pxx, &celldimy, &celldimx, nullptr, nullptr);
    if (pxy == 0 || pxx == 0) return;
    if (celldimy == 0) celldimy = 1;
    if (celldimx == 0) celldimx = 1;

    // Same footer-row reservation as TerminalPixelChart::commit().
    if (!footer_label_.empty() && celldimy > 0 && pxy > celldimy) pxy -= celldimy;

    vector<uint32_t> buffer(static_cast<size_t>(pxy) * pxx, 0xff151515u); // opaque kHeatmapBackground - overwritten below for every pixel regardless, kept consistent for clarity

    std::vector<float> agg_brightness, agg_saturation;
    resampleGrid(brightness_, saturation_, gridCols(), gridRows(), static_cast<int>(pxx), static_cast<int>(pxy), agg_brightness, agg_saturation);

    for (unsigned py = 0; py < pxy; py++) {
      // agg_* row 0 is the BOTTOM (resampleGrid()'s contract), but py=0 is
      // the TOP of the image - flip vertically.
      unsigned vy = pxy - 1 - py;
      for (unsigned px = 0; px < pxx; px++) {
        size_t idx = static_cast<size_t>(vy) * pxx + px;

        HeatmapRgb rgb = heatmapCellColor(agg_saturation[idx], agg_brightness[idx]);
        buffer[py * pxx + px] = (0xffu << 24) | (static_cast<uint32_t>(rgb.b) << 16) | (static_cast<uint32_t>(rgb.g) << 8) | static_cast<uint32_t>(rgb.r);
      }
    }

    for (auto & marker : markers_) {
      unsigned mx = static_cast<unsigned>(marker.u * static_cast<float>(pxx));
      unsigned my_from_bottom = static_cast<unsigned>(marker.v * static_cast<float>(pxy));
      unsigned my = my_from_bottom < pxy ? pxy - 1 - my_from_bottom : 0;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          long y = static_cast<long>(my) + dy, x = static_cast<long>(mx) + dx;
          if (y < 0 || y >= static_cast<long>(pxy) || x < 0 || x >= static_cast<long>(pxx)) continue;
          buffer[static_cast<size_t>(y) * pxx + static_cast<size_t>(x)] = 0xffffffffu; // opaque white
        }
      }
    }

    ncpp::Visual visual(buffer.data(), static_cast<int>(pxy), static_cast<int>(pxx * 4), static_cast<int>(pxx));
    ncvisual_options vopts{};
    vopts.n = native_plane;
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;
    visual.blit(&vopts);

    // Axis labels overlay the already-blitted image (same "putstr after
    // blit still shows through" the footer label below already relies on)
    // - always drawn (see axisLabels()'s own comment): each label
    // character's pixel footprint is collapsed to a single mean color used
    // as its background, with the glyph itself in white-blended-with-
    // transparency (labelForegroundColor()) over that background.
    auto [rows, cols] = getDim();
    int usable_rows = footer_label_.empty() ? static_cast<int>(rows) : static_cast<int>(rows) - 1;
    for (auto & label : axisLabels(usable_rows, static_cast<int>(cols))) {
      std::string text = label.text;
      for (int c = 0; c < static_cast<int>(text.size()); c++) {
        int col = label.col + c;
        if (col < 0 || col >= static_cast<int>(cols)) continue;
        unsigned px0 = static_cast<unsigned>(col) * celldimx;
        unsigned px1 = min(pxx, px0 + celldimx);
        unsigned py0 = static_cast<unsigned>(label.row) * celldimy;
        unsigned py1 = min(pxy, py0 + celldimy);

        double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
        unsigned count = 0;
        for (unsigned py = py0; py < py1; py++) {
          for (unsigned px = px0; px < px1; px++) {
            uint32_t pixel = buffer[py * pxx + px];
            sum_r += static_cast<float>(pixel & 0xffu);
            sum_g += static_cast<float>((pixel >> 8) & 0xffu);
            sum_b += static_cast<float>((pixel >> 16) & 0xffu);
            count++;
          }
        }
        if (count == 0) continue;
        HeatmapRgb mean{static_cast<float>(sum_r / count), static_cast<float>(sum_g / count), static_cast<float>(sum_b / count)};

        uint8_t fr, fg, fb;
        labelForegroundColor(mean, fr, fg, fb);
        setFgColor(fr, fg, fb);
        setBgColor(static_cast<int>(mean.r), static_cast<int>(mean.g), static_cast<int>(mean.b));
        putstr(label.row, col, std::string(1, text[static_cast<size_t>(c)]));
      }
    }

    if (!footer_label_.empty()) {
      putstr(static_cast<int>(rows) - 1, 0, footer_label_);
    }
  }

private:
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
};

void
TerminalUI::initialize(std::shared_ptr<Controller> & controller) {
  auto root_plane = make_unique<TerminalPlane>(controller, nc->get_stdplane(), false);
  setPlane(std::move(root_plane));

  setFgColor(styles_.window_fg_color);
  setBgColor(styles_.window_bg_color);
  fill();

  menu_ = make_shared<TerminalMenu>();

  bool use_pixel = notcurses_check_pixel_support(*nc) != NCPIXEL_NONE;
  auto make_chart = [&](Chart::ChartType type, double min_y, double max_y) -> shared_ptr<Chart> {
    if (use_pixel) return make_shared<TerminalPixelChart>(getPlane(), type, min_y, max_y);
    else return make_shared<TerminalChart>(getPlane(), type, min_y, max_y);
  };
  chart_ = make_chart(Chart::DOTS, 0.0, 0.0);
  volume_meter_ = make_chart(Chart::DOTS, -100, 0);

  if (use_pixel) heatmap_ = make_shared<TerminalPixelHeatmapChart>(getPlane(), DiracAnalyzer::kAzimuthBins, DiracAnalyzer::kElevationBins);
  else heatmap_ = make_shared<TerminalHeatmapChart>(getPlane(), DiracAnalyzer::kAzimuthBins, DiracAnalyzer::kElevationBins);

  // No footer legend on either scope: the heatmap's axis extremes are
  // drawn directly in the grid area instead (axisLabels(), above), and the
  // FFT chart's "Spectrum: 0-{nyquist}Hz" label was removed the same way,
  // reclaiming the row a legend would have used.

  UI::initialize();

  layout();

  nc->render();
}

void
TerminalUI::render() {
  nc->render();
}

void
TerminalUI::refresh() {
  nc->refresh(nullptr, nullptr);
}

bool
TerminalUI::readInput() {
  ncinput ni;

  // Applies the same id/modifier massaging this loop has always done, then
  // constructs and dispatches one InputEvent - factored out so a byte the
  // KP-escape-sequence recognizer below ends up replaying (see
  // kp_escape_depth_'s own comment) goes through identical processing to
  // one that was never buffered at all.
  auto dispatchRawKey = [this](int raw_id, int y, int x, unsigned modifiers, InputEvent::Kind kind) {
    bool alt = modifiers & NCKEY_MOD_ALT;
    bool shift = modifiers & NCKEY_MOD_SHIFT;
    bool ctrl = modifiers & NCKEY_MOD_CTRL;
    bool meta = modifiers & NCKEY_MOD_META;

    int id = raw_id;
    if (id >= 'A' && id <= 'Z') {
      id = tolower(id);
      if (!ctrl) shift = true; // fix bug in notcurses
    } else if (id == 28) {
      ctrl = true;
      alt = meta = shift = false;
      id = '\\';
    }

    InputEvent input(id, y, x, alt, shift, ctrl, meta, kind);
    offerInput(input);
  };

  // Replays every byte the KP-escape recognizer had buffered so far, in
  // order, then resets it - used whenever a partial match turns out not
  // to be the sequence after all.
  auto flushPendingKpEscape = [this, &dispatchRawKey]() {
    for (int i = 0; i < kp_escape_depth_; i++) {
      auto & p = kp_escape_pending_[i];
      dispatchRawKey(p.id, p.y, p.x, p.modifiers, p.kind);
    }
    kp_escape_depth_ = 0;
  };

  while (nc->get(false, &ni) > 0) {
    // Legacy terminals only ever report NCTYPE_UNKNOWN (no press/release
    // distinction - notcurses's own signal that this terminal never
    // negotiated the Kitty keyboard protocol at all, no separate
    // capability query needed); the Kitty keyboard protocol (kitty, foot,
    // wezterm, ghostty, ...) reports real NCTYPE_PRESS/NCTYPE_REPEAT/
    // NCTYPE_RELEASE events for the same physical keystroke. Mapped to
    // its own distinct InputEvent::Kind::UNKNOWN (not silently folded
    // into PRESS) - see that enum's own doc comment for why: code that
    // tracks "is this key still held" cannot infer anything of the kind
    // from an UNKNOWN-kind terminal, since it has no way to ever learn
    // that the key was released. RELEASE now reaches offerInput() (it
    // didn't used to - see InputEvent::Kind's own doc comment):
    // UIElement::dispatchCommand() ignores it outright so no keymap-bound
    // command double-fires the way plain presses used to (the bug this
    // code used to guard against by dropping RELEASE entirely), but
    // PatternEditor's own raw note-entry code needs to see it, to send a
    // note-off when a held note key is physically released.
    auto kind = ni.evtype == NCTYPE_RELEASE ? InputEvent::Kind::RELEASE
              : ni.evtype == NCTYPE_REPEAT ? InputEvent::Kind::REPEAT
              : ni.evtype == NCTYPE_PRESS ? InputEvent::Kind::PRESS
              : InputEvent::Kind::UNKNOWN;

    // Legacy xterm/VT220 "SS3 + modifier digit" encoding some terminals
    // still send for a Ctrl-modified numeric-keypad Divide/Multiply
    // keystroke instead of the Kitty keyboard protocol (confirmed via
    // notcurses-input against a real terminal: notcurses's own
    // escape-sequence lexer doesn't recognize this pattern as a single
    // key, so all 4 bytes - ESC, 'O', '5', then 'o' or 'j' - arrive as
    // their own separate, unmodified raw events). Recognized here and
    // turned into one synthetic Ctrl-modified InputEvent
    // (NCKEY_KP_DIVIDE/NCKEY_KP_MULTIPLY) instead of leaking "O5o"/"O5j"
    // through as if it had been typed into the pattern grid. A byte that
    // breaks a partial match falls through to normal dispatch below (via
    // flushPendingKpEscape() replaying whatever was buffered first), so a
    // real standalone Escape or Esc-then-x for M-x still work exactly as
    // before.
    if (kp_escape_depth_ == 0 && ni.id == NCKEY_ESC) {
      kp_escape_pending_[0] = { static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind };
      kp_escape_depth_ = 1;
      continue;
    } else if (kp_escape_depth_ == 1) {
      if (ni.id == 'O') {
	kp_escape_pending_[1] = { static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind };
	kp_escape_depth_ = 2;
	continue;
      }
      flushPendingKpEscape();
    } else if (kp_escape_depth_ == 2) {
      if (ni.id == '5') {
	kp_escape_pending_[2] = { static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind };
	kp_escape_depth_ = 3;
	continue;
      }
      flushPendingKpEscape();
    } else if (kp_escape_depth_ == 3) {
      if (ni.id == 'o' || ni.id == 'j') {
	kp_escape_depth_ = 0;
	InputEvent input(ni.id == 'o' ? NCKEY_KP_DIVIDE : NCKEY_KP_MULTIPLY, ni.y, ni.x, false, false, true, false, kind);
	offerInput(input);
	continue;
      }
      flushPendingKpEscape();
    }

    dispatchRawKey(static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind);
  }

  return true;
}

void
TerminalUI::startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) {
  int out_pipe[2];

  if (pipe(out_pipe) != 0) { // make a pipe
    exit(1);
  }

  // stderr always gets redirected into the pipe below (so it can be shown
  // on the status line) - but if the caller already redirected it
  // themselves (2> some.log, or piped into another process), that
  // destination is the one place a person can actually go read a full,
  // unbounded, post-mortem log after the fact; the status line only ever
  // shows the latest single line, and only while the UI is still up. Save
  // a duplicate of it here, before it's overwritten, so every future write
  // to fd 2 can still reach it too (see the out_pipe-draining branch
  // below) - a real terminal (isatty true, ordinary interactive use, no
  // redirection) has no such separate destination to preserve, so this
  // stays -1 and nothing is duplicated.
  int real_stderr_fd = isatty(STDERR_FILENO) ? -1 : dup(STDERR_FILENO);

  dup2(out_pipe[1], STDERR_FILENO); // redirect stderr to the pipe
  close(out_pipe[1]);

  size_t num_midi_capture_desc = audio.getMidiCaptureDescriptors().size();
  auto launchpad_descriptors = launchpad_io.getPollDescriptors();
  size_t num_launchpad_desc = launchpad_descriptors.size();
  size_t midi_base = 3;
  size_t launchpad_base = midi_base + num_midi_capture_desc;
  size_t num_descriptors = launchpad_base + num_launchpad_desc;
  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);

#if 1
  descriptors[0].fd = nc->get_inputready_fd();
#else
  descriptors[0].fd = 0;
#endif
  descriptors[0].events = POLLIN;

  descriptors[1].fd = getController().getUIEventQueue().getPollFd();
  descriptors[1].events = POLLIN;

  descriptors[2].fd = out_pipe[0];
  descriptors[2].events = POLLIN;

  for (size_t i = 0; i < num_midi_capture_desc; i++) {
    descriptors[midi_base + i] = audio.getMidiCaptureDescriptors()[i];
  }

  for (size_t i = 0; i < num_launchpad_desc; i++) {
    descriptors[launchpad_base + i] = launchpad_descriptors[i];
  }

  // setStatus("Starting... nd = " + to_string(num_descriptors));

  time_t prev_update = 0;
  renderComponents(true);

  string waiting_stderr;
  
  while ( !close_ui_ ) {
    bool render = false;
    
    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (i == 0) {
	    render |= readInput();
	  } else if (i == 1) {
	    auto event = getController().getUIEventQueue().pop();
	    handleEvent(*event);
	    if (event->needRedraw()) render = true;
	    while ( getController().getUIEventQueue().hasEvents() ) {
	      auto event = getController().getUIEventQueue().pop();	      
	      handleEvent(*event);
	      if (event->needRedraw()) render = true;
	    }
	  } else if (i == 2) {
	    char buffer[4096];
	    int r = read(out_pipe[0], buffer, 4096);
	    if (real_stderr_fd >= 0 && r > 0) {
	      // Raw bytes, not the line-split/status-line text below - this
	      // is a faithful mirror of what stderr would have received
	      // without the redirect above, not a reformatted copy. Loops to
	      // cover a partial write (a real possibility for a pipe/socket
	      // destination); best-effort otherwise - a write() failure here
	      // (e.g. the destination process on the other end of a pipe
	      // already exited) must never take the whole UI down with it.
	      int written = 0;
	      while (written < r) {
		auto n = write(real_stderr_fd, buffer + written, static_cast<size_t>(r - written));
		if (n <= 0) break;
		written += static_cast<int>(n);
	      }
	    }
	    waiting_stderr += string(buffer, r);
	    while ( 1 ) {
	      auto pos = waiting_stderr.find('\n');
	      if (pos != string::npos) {
		setStatus(waiting_stderr.substr(0, pos));
		waiting_stderr.erase(0, pos + 1);
	      } else {
		break;
	      }
	    }
	  } else if (i < launchpad_base) {
	    auto evs = audio.recordMIDI();
	    // setStatus("got midi events: " + to_string(evs.size()));

	    for (auto & ev : evs) {
	      handleEvent(ev);
	      if (ev.needRedraw()) render = true;
	    }
	  } else {
	    auto evs = launchpad_io.pollEvents();

	    for (auto & ev : evs) {
	      handleEvent(*ev);
	      if (ev->needRedraw()) render = true;
	    }
	  }
	}
      }
      
      render |= renderComponents();

      if (render) {
	nc->render();
      }
    }
  }
}
