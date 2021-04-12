#include "TerminalUI.h"

#include "Synth.h"
#include "AudioAPI.h"
#include "SampleData.h"
#include "UIInput.h"

#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cassert>
#include <unistd.h>
#include <memory>
#include <cmath>
#include <fmt/core.h>

#include <sys/time.h>
#include <iostream>

using namespace ncpp;
using namespace std;
using namespace fmt;

static inline ncinput to_ncinput(const UIInput & input) {
  ncinput ni = { .id = input.getId(), .y = -1, .x = -1, .alt = input.hasAlt(), .shift = input.hasShift(), .ctrl = input.hasCtrl(), .seqnum = input.getSeqnum() };
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
  TerminalPlane(Plane * _plane, bool _owner = true) : plane(_plane), owner(_owner) { }
  ~TerminalPlane() {
    if (owner) delete plane;
  }
  void move(int y, int x) override { plane->move(y, x); }
  void resize(int rows, int cols) override { plane->resize(rows, cols); }
  void setFgColor(int r, int g, int b) override { plane->set_fg_rgb8(r, g, b); }
  void setBgColor(int r, int g, int b) override { plane->set_bg_rgb8(r, g, b); }
  void erase() override { plane->erase(); }
  void putstr(int y, int x, std::string s) override { plane->putstr(y, x, s.c_str()); }
  unique_ptr<UIPlane> createChild() override {
    auto plane = new Plane(1, 1, 0, 0);
    plane->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
    // plane->set_scrolling(true);
    // plane->rounded_box(NCSTYLE_NONE, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20), 0, 0, 0);
    // plane->putstr("");
    return make_unique<TerminalPlane>(plane);
  }

  pair<int, int> getDim() const override {
    int y, x;
    plane->get_dim(&y, &x);
    return pair(y, x);
  }

  void setOwning(bool t) { owner = t; }

  Plane & getPlane() { return *plane; }

private:
  Plane * plane;
  bool owner;
};

class TerminalMenu : public UIMenu {
public:
  TerminalMenu() {
    ncmenu_item file_items[] = { { .desc = "New", .shortcut = { .id = 'N' } },
				 { .desc = "Open", .shortcut = { .id = 'O' } },
				 { .desc = "Quit", .shortcut = { .id = 'q' } }
    };
    
    ncmenu_section sections[] = { { .name = "File", .itemcount = 3, .items = file_items, .shortcut = { .id = 'f', .alt = true } }
    };
    uint64_t headerchannels = 0;                                                  
    uint64_t sectionchannels = 0;                                                 
    channels_set_fg_rgb(&sectionchannels, 0xffffff);                              
    channels_set_bg_rgb(&sectionchannels, 0x000000);                              
    channels_set_fg_alpha(&sectionchannels, CELL_ALPHA_HIGHCONTRAST);             
    channels_set_bg_alpha(&sectionchannels, CELL_ALPHA_BLEND);                    
    channels_set_fg_rgb(&headerchannels, 0xffffff);                               
    channels_set_bg_rgb(&headerchannels, 0x7f347f);                               
    channels_set_bg_alpha(&headerchannels, CELL_ALPHA_BLEND);
    ncmenu_options mopts = { .sections = sections, .sectioncount = 1, .headerchannels = headerchannels, .sectionchannels = sectionchannels, .flags = 0 };
    menu = make_unique<Menu>(&mopts);    
  }

  bool offerInput(const UIInput & input) override {
    auto ni = to_ncinput(input);
    return menu->offer_input(&ni);
  }
  
private:
  unique_ptr<Menu> menu;
};

class TerminalChart : public Chart {
public:
  TerminalChart(UIPlane & _parent) : Chart(_parent) { }

  void setSample(int i, double v) override {
    if (!plot) {
      auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
      tplane.setOwning(false);
      
      ncplot_options opts;
      memset(&opts, 0, sizeof(opts));
      opts.flags = 0
	// | NCPLOT_OPTION_LABELTICKSD
	// | NCPLOT_OPTION_EXPONENTIALD
	// | NCPLOT_OPTION_PRINTSAMPLE
	;
      opts.gridtype = NCBLIT_BRAILLE;
      // opts.gridtype = NCBLIT_8x1;
      
      channels_set_fg_rgb8(&opts.minchannels, 0x80, 0x80, 0xff);
      channels_set_bg_rgb(&opts.minchannels, 0x201020);
      channels_set_bg_alpha(&opts.minchannels, CELL_ALPHA_BLEND);
      channels_set_fg_rgb8(&opts.maxchannels, 0x80, 0xff, 0x80);
      channels_set_bg_rgb(&opts.maxchannels, 0x201020);
      channels_set_bg_alpha(&opts.maxchannels, CELL_ALPHA_BLEND);
      
      plot = std::make_shared<PlotD>(tplane.getPlane(), &opts);
    }

    auto [rows, columns] = getDim();
    plot->set_sample(columns - i - 1, v);
  }
  
private:
  std::shared_ptr<PlotD> plot;
};

class TerminalStatusLine : public StatusLine {
public:
  TerminalStatusLine(UIPlane & parent) : StatusLine(parent) { }

  bool offerInput(const UIInput & input) override {
    if (readerActive()) {
      if (input.getId() == NCKEY_ENTER) {
	string cmd = closeReader();	       
      } else if (input.hasCtrl() && input.getId() == 'g') {
	closeReader();	
      } else {
	auto ni = to_ncinput(input);
	ncreader_offer_input(reader, &ni);
      }
      return true;
    } else if (input.getId() == 0x1b) {
      meta_pressed = true;
    } else if (meta_pressed) {
      if (input.getId() == 'x' || input.getId() == 'X') {
	showReader();
	return true;
      }
      meta_pressed = false;
    }
    return false;
  }

protected:
  bool readerActive() const { return reader != 0; }

  string closeReader() {
    char* contents;
    ncreader_destroy(reader, &contents);
    string r = contents;
    free(contents);    
    reader = 0;
    setMessage("");
    return r;
  }
  
  void showReader() {
    if (!readerActive()) {
      auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
      tplane.setOwning(false);
      
      setMessage("M-x ");
      
      ncreader_options reader_opts;
      reader_opts.tchannels = 0;
      channels_set_fg_rgb(&reader_opts.tchannels, 0xffffff);
      channels_set_bg_rgb(&reader_opts.tchannels, 0x000000);
      channels_set_fg_alpha(&reader_opts.tchannels, CELL_ALPHA_HIGHCONTRAST);
      channels_set_bg_alpha(&reader_opts.tchannels, CELL_ALPHA_BLEND);
      reader_opts.tattrword = 0; // attributes used for input
      reader_opts.flags = NCREADER_OPTION_CURSOR | NCREADER_OPTION_HORSCROLL;

      auto [rows, cols] = getDim();
      auto reader_plane = ncplane_new(tplane.getPlane().to_ncplane(), rows, cols, 0, 4, nullptr, nullptr);
      ncplane_set_fg_rgb8(reader_plane, 0x80, 0xc0, 0x80);
      ncplane_set_bg_rgb8(reader_plane, 0x00, 0x40, 0x00);
      ncplane_set_base(reader_plane, "", 0, 0);
      reader = ncreader_create(reader_plane, &reader_opts);
    }
  }
  
private:
  bool meta_pressed = false;
  ncreader * reader = 0;
};
  
void
TerminalUI::initialize() {
#if 0
  if (!setlocale(LC_ALL, "")){
    fprintf(stderr, "Couldn't set locale\n");
    exit(1);
  }
#endif

  if (!nc) {
    notcurses_options nopts{};
    // nopts.flags = NCOPTION_INHIBIT_SETLOCALE;
    nc = make_shared<NotCurses>(nopts);
    nc->mouse_enable();
  }
  
  auto root_plane = make_unique<TerminalPlane>(nc->get_stdplane(), false);

#if 0
  root_plane->cursor_move(5, 0);
  root_plane->hline(Cell('-'), cols);
  root_plane->cursor_move(1, left_width);
  root_plane->vline(Cell('|'), 4);
#endif

  menu = make_shared<TerminalMenu>();
  left_chart = make_shared<TerminalChart>(*root_plane);
  right_chart = make_shared<TerminalChart>(*root_plane);
  
  score_display = make_shared<ScoreDisplay>(*root_plane);

  info_line = make_shared<InfoLine>(*root_plane);
  status_line = make_shared<TerminalStatusLine>(*root_plane);

  layout();
  nc->render(); 
}

void
TerminalUI::setStatus(const std::string & s) {
  status_line->setMessage(s);
  nc->render();
}

void
TerminalUI::layout() {
  int rows, cols;
  nc->get_term_dim(&rows, &cols);

  int left_width = cols / 2;
  int right_width = cols - left_width - 1;

#if 0
  root_plane->cursor_move(5, 0);
  root_plane->hline(Cell('-'), cols);
  root_plane->cursor_move(1, left_width);
  root_plane->vline(Cell('|'), 4);
#endif
  
  left_chart->resize(4, left_width).move(1, 0);
  right_chart->resize(4, right_width).move(1, left_width + 1);
  score_display->resize(rows - 8, cols).move(5, 0);
  info_line->resize(1, cols).move(rows - 2, 0);
  status_line->resize(1, cols - 1).move(rows - 1, 0);
}

static inline int keyToNote(int key) {
  switch (key) {
  case 'q': return 60;
  case '2': return 61;
  case 'w': return 62;
  case '3': return 63;
  case 'e': return 64;
  case 'r': return 65;
  case '5': return 66;
  case 't': return 67;
  case '6': return 68;
  case 'y': return 69;
  case '7': return 70;
  case 'u': return 71;
  case '8': return 72;
  case 'i': return 73;
  }
  return -1;
}

bool
TerminalUI::offerInput(const UIInput & input) {
  // if (ni.ctrl && ni.id == 'L') notcurses_refresh(*nc, NULL, NULL);
  // if (ni.ctrl && (ni.id == 'q' || ni.id == 'Q')) close_ui = true;
  if (input.getId() == NCKEY_RESIZE) {
    layout();
    nc->refresh(nullptr, nullptr);    
  } else if (input.getId() == ' ') {
    if (getSynth()->togglePlayback()) {
      setStatus("Playing");
    } else {
      setStatus("Stopped");
    }
    return true;
  } else {
    int note = keyToNote(input.getId());
    if (note != -1) {
      getSynth()->getSong().getInstrument(10).playNote(note);
      setStatus(format("Playing {}", note));
      return true;
    }
  }
  
  return false;
}

bool
TerminalUI::readInput() {
  bool render = false;
  
  ncinput ni;
  if (nc->getc(true, &ni) != (char32_t)-1) {
    bool handled = false;
    UIInput input(ni.seqnum, ni.id, ni.alt, ni.shift, ni.ctrl);
    if (!handled) handled |= menu->offerInput(input);
    if (!handled) handled |= status_line->offerInput(input);
    if (!handled) handled |= score_display->offerInput(input);
    if (!handled) handled |= offerInput(input);
  }

  return true;
}

void
TerminalUI::start(AudioAPI & audio) {
  size_t num_descriptors = 1 + audio.getPollDescriptors().size();
  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);
  
  descriptors[0].fd = nc->get_inputready_fd();
  descriptors[0].events = POLLIN;

  for (size_t i = 0; i < audio.getPollDescriptors().size(); i++) {
    descriptors[1 + i] = audio.getPollDescriptors()[i];
  }
    
  // setStatus("Starting... nd = " + to_string(num_descriptors));

  time_t prev_update = 0;
  time_t prev_pos = getSynth()->getCurrentPosition();

  score_display->render(*getSynth(), true);
  info_line->render(*getSynth(), true);
	
  while ( !close_ui ) {
    bool render = false;
    
    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, 100) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (d.fd == 0) {
	    render |= readInput();
	  } else {
	    auto data = getSynth()->play(audio.getFrameCount());
	    audio.play(data, *this);

	    time_t current_time = now();

	    // waiting_data.clear();
	    waiting_data.append(data);
	    // setStatus(to_string(data.size()) + " " + to_string(waiting_data.size()));
	    
	    if (prev_update + 100 < current_time) {
	      prev_update = current_time;

	      waiting_data.shortenToPowerofTwo();

	      left_chart->displayFFT(waiting_data, 0);
	      right_chart->displayFFT(waiting_data, 1);

	      waiting_data.clear();
	      	
	      render = true;	      
	    }
	  }
	}
      }

      if (prev_pos != getSynth()->getCurrentPosition()) {
	info_line->render(*getSynth());
	render = true;
      }
      
      render |= score_display->render(*getSynth());
      
      prev_pos = getSynth()->getCurrentPosition();

      if (render) {
	nc->render();
      }
    }
  }  
}
