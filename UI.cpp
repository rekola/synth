#include "UI.h"

#include "Synth.h"
#include "AudioAPI.h"
#include "FFT.h"
#include "SampleData.h"

#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cassert>
#include <unistd.h>
#include <memory>
#include <cmath>

#include <sys/time.h>

using namespace ncpp;
using namespace std;

static inline long long now() {
  struct timeval tv;
  int r = gettimeofday(&tv, 0);
  if (r == 0) {
    return (long long)1000 * tv.tv_sec + tv.tv_usec / 1000;
  } else {
    return 0;
  }
}

UI::~UI() {

}

void
UI::initialize() {
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
  
  auto root_plane = nc->get_stdplane();

  int rows, cols;
  nc->get_term_dim(&rows, &cols);

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
  menu = std::make_shared<Menu>(&mopts);
  
  top_line = std::make_shared<Plane>(root_plane, 1, cols, 1, 0);
  top_line->set_fg_rgb8(0xc0, 0x80, 0xc0);
  top_line->set_bg_rgb8(0x20, 0x10, 0x20);
  top_line->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
  top_line->putstr("Music Editor");

  int left_width = cols / 2;
  int right_width = cols - left_width - 1;

  root_plane->cursor_move(8, 0);
  root_plane->hline(Cell('-'), cols);
  root_plane->cursor_move(2, left_width);
  root_plane->vline(Cell('|'), 6);

  left_plot_plane = std::make_shared<Plane>(6, left_width, 2, 0);
  left_plot_plane->set_fg_rgb8(0x80, 0xc0, 0x80);
  left_plot_plane->set_bg_rgb8(0x20, 0x10, 0x20);
  left_plot_plane->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0x10, 0x20));

  right_plot_plane = std::make_shared<Plane>(6, right_width, 2, left_width + 1);
  right_plot_plane->set_fg_rgb8(0x80, 0xc0, 0x80);
  right_plot_plane->set_bg_rgb8(0x20, 0x10, 0x20);
  right_plot_plane->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0x10, 0x20));

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
    
  left_plot = std::make_shared<PlotD>(*left_plot_plane, &opts);
  right_plot = std::make_shared<PlotD>(*right_plot_plane, &opts);

  score_plane = std::make_shared<Plane>(rows - 11, cols, 9, 0);
  score_plane->set_fg_rgb8(0xc0, 0x80, 0xc0);
  score_plane->set_bg_rgb8(0x20, 0x00, 0x20);
  score_plane->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
  score_plane->set_scrolling(true);
  // score_plane->rounded_box(NCSTYLE_NONE, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20), 0, 0, 0);
  
  // score_plane->putstr("");
  
  info_line = std::make_shared<Plane>(1, cols, rows - 2, 0);
  info_line->set_fg_rgb8(0x80, 0xc0, 0x80);
  info_line->set_bg_rgb8(0x00, 0x40, 0x00);
  info_line->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));

  status_line = std::make_shared<Plane>(1, cols - 1, rows - 1, 0);
  status_line->set_fg_rgb8(0x80, 0xc0, 0x80);
  status_line->set_bg_rgb8(0x00, 0x40, 0x00);
  status_line->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));

#if 0
  ncreader_options reader_opts;
  reader_opts.tchannels = 0; // channels used for input                                                      
  reader_opts.tattrword = 0; // attributes used for input                                                    
  reader_opts.flags = 0;     // bitfield of NCREADER_OPTION_*                                                
  
  reader = std::make_shared<Reader>(status_line, &reader_opts);
#endif
  
  nc->render();
}

void
UI::setStatus(const std::string & s) {
  status_line->erase();
  status_line->putstr(s.c_str());
  nc->render();
}

void
UI::readInput(Synth & synth) {
  ncinput ni;
  if (nc->getc(true, &ni) != (char32_t)-1) {
    if (ni.ctrl && ni.id == 'L') {
      notcurses_refresh(*nc, NULL, NULL);
    } else if (ni.id == 'q' || ni.id == 'Q') {
      close_ui = true;
    } else if (ni.id == ' ') {
      if (synth.togglePlayback()) {
	setStatus("Playing");
      } else {
	setStatus("Stopped");
      }
    }

    if (menu) {
      menu->offer_input(&ni);
    }
  }
}

void
UI::start(Synth & synth, AudioAPI & audio) {
  size_t num_descriptors = 1 + audio.getPollDescriptors().size();
  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);
  
  descriptors[0].fd = 0;
  descriptors[0].events = POLLIN;

  for (size_t i = 0; i < audio.getPollDescriptors().size(); i++) {
    descriptors[1 + i] = audio.getPollDescriptors()[i];
  }
    
  // setStatus("Starting... nd = " + to_string(num_descriptors));

  time_t prev_update = 0;
  
  while ( !close_ui ) {
    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {

      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (d.fd == 0) {
	    readInput(synth);
	  } else {
	    auto data = synth.play(audio.getFrameCount());
	    audio.play(data, *this);

	    int rows, cols;
	    nc->get_term_dim(&rows, &cols);
	    time_t current_time = now();

	    waiting_data.clear();
	    waiting_data.append(data);
	    setStatus(to_string(data.size()) + " " + to_string(waiting_data.size()));
	    
	    if (prev_update + 100 < current_time) {
	      prev_update = current_time;

	      auto fft_left = FFT::perform(waiting_data, 0, cols);
	      auto fft_right = FFT::perform(waiting_data, 1, cols);

	      waiting_data.clear();

	      for (size_t i = 0; i < fft_left.size(); i++) {
		left_plot->set_sample(fft_left.size() - i - 1, fft_left[i]);
	      }
	      for (size_t i = 0; i < fft_right.size(); i++) {
		right_plot->set_sample(fft_right.size() - i - 1, fft_right[i]);
	      }
	      
	      nc->render();
	    }
	  }
	}
      }            
    }
  }  
}
