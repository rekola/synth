#include "TerminalUI.h"

#include "InputEvent.h"
#include "Controller.h"
#include "UIMenu.h"
#include "Chart.h"
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
  
  void showReader(const std::string & prompt = "") override {
    if (!readerActive()) {
      setOwning(false);

      // The reader plane below is opaque and covers its own bounds, so any
      // prompt text must be drawn onto *this* (the still-visible underlying
      // plane) first, and the reader plane offset past it - otherwise the
      // prompt is drawn then immediately hidden under the reader, and the
      // whole M-x minibuffer silently looks like it never opened even
      // though it's actually active and correctly accepting input.
      if (!prompt.empty()) putstr(0, 0, prompt);
      auto prompt_width = static_cast<unsigned int>(prompt.size());

      ncreader_options reader_opts;
      reader_opts.tchannels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0x00, 0x00, 0x00);
      ncchannels_set_fg_alpha(&reader_opts.tchannels, NCALPHA_HIGHCONTRAST);
      ncchannels_set_bg_alpha(&reader_opts.tchannels, NCALPHA_BLEND);
      reader_opts.tattrword = 0; // attributes used for input
      reader_opts.flags = NCREADER_OPTION_CURSOR | NCREADER_OPTION_HORSCROLL;

      auto [rows, cols] = getDim();
      auto reader_cols = static_cast<unsigned int>(cols) > prompt_width ?
	static_cast<unsigned int>(cols) - prompt_width : 1u;

      ncplane_options opts = {
	// 0, 4, nullptr, nullptr);
	.y = 0,
	.x = static_cast<int>(prompt_width),
	.rows = static_cast<unsigned int>(rows),
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
  TerminalChart(UIPlane & parent, ChartType type, double min_y = 0.0, double max_y = 0.0) : Chart(parent, type, min_y, max_y) { }

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
      plot_plane_->resize(plot_rows > 0 ? plot_rows : rows, cols);
      plot_plane_->move(y, x);

      auto & tplane = dynamic_cast<TerminalPlane&>(*plot_plane_);
      tplane.setOwning(false);

      ncplot_options opts;
      memset(&opts, 0, sizeof(opts));
      opts.flags = 0
	// | NCPLOT_OPTION_LABELTICKSD
	// | NCPLOT_OPTION_EXPONENTIALD
	// | NCPLOT_OPTION_PRINTSAMPLE
	;
      opts.gridtype = getType() == DOTS ? NCBLIT_BRAILLE : NCBLIT_2x2;
      // opts.gridtype = NCBLIT_8x1;

      opts.minchannels = NCCHANNELS_INITIALIZER(0x80, 0x80, 0xff, 0x20, 0x10, 0x20);
      ncchannels_set_bg_alpha(&opts.minchannels, NCALPHA_BLEND);
      opts.maxchannels = NCCHANNELS_INITIALIZER(0x80, 0xff, 0x80, 0x20, 0x10, 0x20);
      ncchannels_set_bg_alpha(&opts.maxchannels, NCALPHA_BLEND);

      plot_ = std::make_shared<PlotD>(tplane.getPlane(), &opts);
    }

    plot_->set_sample(i, v);
  }

  void commit() override {
    if (!footer_label_.empty()) {
      auto [rows, cols] = getDim();
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

    vector<uint32_t> buffer(static_cast<size_t>(pxy) * pxx, 0); // 0 alpha = transparent

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
  while (nc->get(false, &ni) > 0) {
    bool alt = ni.modifiers & NCKEY_MOD_ALT;
    bool shift = ni.modifiers & NCKEY_MOD_SHIFT;
    bool ctrl = ni.modifiers & NCKEY_MOD_CTRL;
    bool meta = ni.modifiers & NCKEY_MOD_META;
    
    int id = ni.id;
    if (id >= 'A' && id <= 'Z') {
      id = tolower(id);
      if (!ctrl) shift = true; // fix bug in notcurses
    } else if (id == 28) {
      ctrl = true;
      alt = meta = shift = false;
      id = '\\';
    }
    
    InputEvent input(id, ni.y, ni.x, alt, shift, ctrl, meta);
    offerInput(input);
  }

  return true;
}

void
TerminalUI::startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) {
  int out_pipe[2];

  if (pipe(out_pipe) != 0) { // make a pipe
    exit(1);
  }
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
