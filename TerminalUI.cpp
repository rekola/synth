#include "TerminalUI.h"

#include "Synth.h"
#include "AudioAPI.h"
#include "SampleData.h"
#include "UIInput.h"
#include "Controller.h"
#include "UIMenu.h"
#include "Chart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "ScoreDisplay.h"
#include "InstrumentList.h"

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

#include <ncpp/Plane.hh>
#include <ncpp/Plot.hh>
#include <ncpp/Reader.hh>
#include <ncpp/Menu.hh>
#include <ncpp/Selector.hh>

using namespace ncpp;
using namespace std;
using namespace fmt;

static inline ncinput to_ncinput(const UIInput & input) {
  ncinput ni = { .id = input.getId(), .y = input.getY(), .x = input.getX(), .alt = input.hasAlt(), .shift = input.hasShift(), .ctrl = input.hasCtrl(), .seqnum = input.getSeqnum() };
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
    int y, x;
    plane->get_dim(&y, &x);
    setDim(pair(y, x));
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
      plane->move(y, x);
    }
  }
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
    return make_unique<TerminalPlane>(getController(), plane);
  }
  
  void drawBorder() override {
    plane->erase();
    cell ul = CELL_TRIVIAL_INITIALIZER, ur = CELL_TRIVIAL_INITIALIZER;
    cell lr = CELL_TRIVIAL_INITIALIZER, ll = CELL_TRIVIAL_INITIALIZER;
    cell hl = CELL_TRIVIAL_INITIALIZER, vl = CELL_TRIVIAL_INITIALIZER;
    if (cells_rounded_box(plane->to_ncplane(), NCSTYLE_NONE, 0, &ul, &ur, &ll, &lr, &hl, &vl)) {
      return;
    }                       
    ul.channels = CHANNELS_RGB_INITIALIZER(0xf0, 0xc0, 0xc0, 0, 0, 0);
    ur.channels = CHANNELS_RGB_INITIALIZER(0xf0, 0xc0, 0xc0, 0, 0, 0);
    ll.channels = CHANNELS_RGB_INITIALIZER(0xf0, 0xc0, 0xc0, 0, 0, 0);
    lr.channels = CHANNELS_RGB_INITIALIZER(0xf0, 0xc0, 0xc0, 0, 0, 0);
    hl.channels = CHANNELS_RGB_INITIALIZER(0xf0, 0xc0, 0xc0, 0, 0, 0);
    vl.channels = CHANNELS_RGB_INITIALIZER(0xf0, 0xc0, 0xc0, 0, 0, 0);
    cell_set_bg_alpha(&ul, CELL_ALPHA_BLEND);
    cell_set_bg_alpha(&ur, CELL_ALPHA_BLEND);
    cell_set_bg_alpha(&ll, CELL_ALPHA_BLEND);
    cell_set_bg_alpha(&lr, CELL_ALPHA_BLEND);
    cell_set_bg_alpha(&hl, CELL_ALPHA_BLEND);
    cell_set_bg_alpha(&vl, CELL_ALPHA_BLEND);
    if (ncplane_perimeter(plane->to_ncplane(), &ul, &ur, &ll, &lr, &hl, &vl, 0)) {
      cell_release(plane->to_ncplane(), &ul); cell_release(plane->to_ncplane(), &ur); cell_release(plane->to_ncplane(), &hl);
      cell_release(plane->to_ncplane(), &ll); cell_release(plane->to_ncplane(), &lr); cell_release(plane->to_ncplane(), &vl);
      return;
    }
    cell_release(plane->to_ncplane(), &ul); cell_release(plane->to_ncplane(), &ur); cell_release(plane->to_ncplane(), &hl);
    cell_release(plane->to_ncplane(), &ll); cell_release(plane->to_ncplane(), &lr); cell_release(plane->to_ncplane(), &vl);
  }
  
  void showReader() override {
    if (!readerActive()) {
      setOwning(false);
            
      ncreader_options reader_opts;
      reader_opts.tchannels = 0;
      channels_set_fg_rgb(&reader_opts.tchannels, 0xffffff);
      channels_set_bg_rgb(&reader_opts.tchannels, 0x000000);
      channels_set_fg_alpha(&reader_opts.tchannels, CELL_ALPHA_HIGHCONTRAST);
      channels_set_bg_alpha(&reader_opts.tchannels, CELL_ALPHA_BLEND);
      reader_opts.tattrword = 0; // attributes used for input
      reader_opts.flags = NCREADER_OPTION_CURSOR | NCREADER_OPTION_HORSCROLL;

      auto [rows, cols] = getDim();
      auto reader_plane = ncplane_new(getPlane().to_ncplane(), rows, cols, 0, 4, nullptr, nullptr);
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

  void addItem(string id, string label) override {
    if (selector) {
      char * option = new char[id.size() + 1];
      char * desc = new char[label.size() + 1];
      
      strcpy(option, id.c_str());
      strcpy(desc, label.c_str());
      
      ncselector_item item =
	{
	 .option = option,
	 .desc = desc,
	 .opcolumns = 0,
	 .desccolumns = 0,
	};
      selector->additem(&item);
    }
  }

  void clearItems() override {

  }

  bool offerInput(const UIInput & input) override {
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
  TerminalChart(UIPlane & _parent, ChartType _type) : Chart(_parent, _type) { }

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
      opts.gridtype = getType() == DOTS ? NCBLIT_BRAILLE : NCBLIT_2x2;
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
    plot->set_sample(2 * columns - 1 - i, v);
  }
  
private:
  std::shared_ptr<PlotD> plot;
};
  
void
TerminalUI::initialize(std::shared_ptr<Controller> & controller) {
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
  
  auto root_plane = make_unique<TerminalPlane>(controller, nc->get_stdplane(), false);
  setPlane(std::move(root_plane));

  menu = make_shared<TerminalMenu>();
  
  chart = make_shared<TerminalChart>(getPlane(), Chart::DOTS);
  volume_meter = make_shared<TerminalChart>(getPlane(), Chart::DOTS);

  UI::initialize();
  
  layout();
  nc->render();  
}

void
TerminalUI::setStatus(const std::string & s) {
  status_line->setMessage(s);
  nc->render();
}

bool
TerminalUI::offerInput(const UIInput & input) {
  // if (ni.ctrl && ni.id == 'L') notcurses_refresh(*nc, NULL, NULL);
  // if (ni.ctrl && (ni.id == 'q' || ni.id == 'Q')) close_ui = true;
  if (input.getId() == NCKEY_RESIZE) {
    layout();
    nc->refresh(nullptr, nullptr);
  } else if ((input.getId() == 'n' || input.getId() == 'N') && input.hasCtrl()) {
    setStatus("New song");
    getController().createNewSong();
  } else if (input.getId() == ' ') {
    if (getController().getSynth().togglePlayback()) {
      setStatus("Playing");
    } else {
      setStatus("Stopped");
    }
    return true;
  }
  
  return false;
}

bool
TerminalUI::readInput() {
  bool render = false;
  
  ncinput ni;
  if (nc->getc(true, &ni) != (char32_t)-1) {
    bool handled = false;
    UIInput input(ni.seqnum, ni.id, ni.y, ni.x, ni.alt, ni.shift, ni.ctrl);
    if (!handled) {
      handled |= menu->offerInput(input);
      if (handled) setStatus("menu: " + menu->getSelected());
    }
    if (!handled) handled |= status_line->offerInput(input);
    if (!handled) handled |= instrument_list->offerInput(input);
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
  time_t prev_pos = getController().getSynth().getCurrentPosition();

  score_display->render(true);
  instrument_list->render(true);
  info_line->render(true);
	
  while ( !close_ui ) {
    bool render = false;
    
    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, 50) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (d.fd == 0) {
	    render |= readInput();
	  } else {
	    auto data = getController().getSynth().play(getController().getSong(), audio.getFrameCount());
	    audio.play(data, *this);

	    time_t current_time = now();

	    // waiting_data.clear();
	    // waiting_data.append(data);
	    // setStatus(to_string(data.size()) + " " + to_string(waiting_data.size()));
	    
	    if (prev_update + 50 < current_time) {
	      prev_update = current_time;

	      waiting_data.shortenToPowerofTwo();
	      
	      chart->displayFFT(data);
	      auto [left, right] = data.calculateLoudness();
	      volume_meter->setSample(0, left);
	      volume_meter->setSample(1, right);

	      waiting_data.clear();
	      	
	      render = true;	      
	    }
	  }
	}
      }

      if (prev_pos != getController().getSynth().getCurrentPosition()) {
	info_line->render();
	render = true;
      }
      
      render |= score_display->render();
      
      prev_pos = getController().getSynth().getCurrentPosition();

      if (render) {
	nc->render();
      }
    }
  }  
}
