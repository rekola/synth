#include "TerminalUI.h"

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
#include <fmt/core.h>

#include <sys/time.h>

using namespace ncpp;
using namespace std;
using namespace fmt;

static inline long long now() {
  struct timeval tv;
  int r = gettimeofday(&tv, 0);
  if (r == 0) {
    return (long long)1000 * tv.tv_sec + tv.tv_usec / 1000;
  } else {
    return 0;
  }
}

TerminalUI::TerminalUI() {
  midi_note_names[127] = "G-9";
  midi_note_names[126] = "F#9";
  midi_note_names[125] = "F-9";
  midi_note_names[124] = "E-9";
  midi_note_names[123] = "D#9";
  midi_note_names[122] = "D-9";
  midi_note_names[121] = "C#9";
  midi_note_names[120] = "C-9";
  midi_note_names[119] = "B-8";
  midi_note_names[118] = "A#8";
  midi_note_names[117] = "A-8";
  midi_note_names[116] = "G#8";
  midi_note_names[115] = "G-8";
  midi_note_names[114] = "F#8";
  midi_note_names[113] = "F-8";
  midi_note_names[112] = "E-8";
  midi_note_names[111] = "D#8";
  midi_note_names[110] = "D-8";
  midi_note_names[109] = "C#8";
  midi_note_names[108] = "C-8";
  midi_note_names[107] = "B-7";
  midi_note_names[106] = "A#7";
  midi_note_names[105] = "A-7";
  midi_note_names[104] = "G#7";
  midi_note_names[103] = "G-7";
  midi_note_names[102] = "F#7";
  midi_note_names[101] = "F-7";
  midi_note_names[100] = "E-7";
  midi_note_names[99] = "D#7";
  midi_note_names[98] = "D-7";
  midi_note_names[97] = "C#7";
  midi_note_names[96] = "C-7";
  midi_note_names[95] = "B-6";
  midi_note_names[94] = "A#6";
  midi_note_names[93] = "A-6";
  midi_note_names[92] = "G#6";
  midi_note_names[91] = "G-6";
  midi_note_names[90] = "F#6";
  midi_note_names[89] = "F-6";
  midi_note_names[88] = "E-6";
  midi_note_names[87] = "D#6";
  midi_note_names[86] = "D-6";
  midi_note_names[85] = "C#6";
  midi_note_names[84] = "C-6";
  midi_note_names[83] = "B-5";
  midi_note_names[82] = "A#5";
  midi_note_names[81] = "A-5";
  midi_note_names[80] = "G#5";
  midi_note_names[79] = "G-5";
  midi_note_names[78] = "F#5";
  midi_note_names[77] = "F-5";
  midi_note_names[76] = "E-5";
  midi_note_names[75] = "D#5";
  midi_note_names[74] = "D-5";
  midi_note_names[73] = "C#5";
  midi_note_names[72] = "C-5";
  midi_note_names[71] = "B-4";
  midi_note_names[70] = "A#4";
  midi_note_names[69] = "A-4";
  midi_note_names[68] = "G#4";
  midi_note_names[67] = "G-4";
  midi_note_names[66] = "F#4";
  midi_note_names[65] = "F-4";
  midi_note_names[64] = "E-4";
  midi_note_names[63] = "D#4";
  midi_note_names[62] = "D-4";
  midi_note_names[61] = "C#4";
  midi_note_names[60] = "C-4";
  midi_note_names[59] = "B-3";
  midi_note_names[58] = "A#3";
  midi_note_names[57] = "A-3";
  midi_note_names[56] = "G#3";
  midi_note_names[55] = "G-3";
  midi_note_names[54] = "F#3";
  midi_note_names[53] = "F-3";
  midi_note_names[52] = "E-3";
  midi_note_names[51] = "D#3";
  midi_note_names[50] = "D-3";
  midi_note_names[49] = "C#3";
  midi_note_names[48] = "C-3";
  midi_note_names[47] = "B-2";
  midi_note_names[46] = "A#2";
  midi_note_names[45] = "A-2";
  midi_note_names[44] = "G#2";
  midi_note_names[43] = "G-2";
  midi_note_names[42] = "F#2";
  midi_note_names[41] = "F-2";
  midi_note_names[40] = "E-2";
  midi_note_names[39] = "D#2";
  midi_note_names[38] = "D-2";
  midi_note_names[37] = "C#2";
  midi_note_names[36] = "C-2";
  midi_note_names[35] = "B-1";
  midi_note_names[34] = "A#1";
  midi_note_names[33] = "A-1";
  midi_note_names[32] = "G#1";
  midi_note_names[31] = "G-1";
  midi_note_names[30] = "F#1";
  midi_note_names[29] = "F-1";
  midi_note_names[28] = "E-1";
  midi_note_names[27] = "D#1";
  midi_note_names[26] = "D-1";
  midi_note_names[25] = "C#1";
  midi_note_names[24] = "C-1";
  midi_note_names[23] = "B-0";
  midi_note_names[22] = "A#0";
  midi_note_names[21] = "A-0";
}

TerminalUI::~TerminalUI() {

}

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

  root_plane->cursor_move(6, 0);
  root_plane->hline(Cell('-'), cols);
  root_plane->cursor_move(2, left_width);
  root_plane->vline(Cell('|'), 4);

  left_plot_plane = std::make_shared<Plane>(4, left_width, 2, 0);
  left_plot_plane->set_fg_rgb8(0x80, 0xc0, 0x80);
  left_plot_plane->set_bg_rgb8(0x20, 0x10, 0x20);
  left_plot_plane->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0x10, 0x20));

  right_plot_plane = std::make_shared<Plane>(4, right_width, 2, left_width + 1);
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

  score_plane = std::make_shared<Plane>(rows - 9, cols, 7, 0);
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
TerminalUI::setStatus(const std::string & s) {
  status_line->erase();
  status_line->putstr(s.c_str());
  nc->render();
}

void
TerminalUI::readInput(Synth & synth) {
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
TerminalUI::renderInfo(Synth & synth) {
  auto s0 = format("{:02x}", synth.getCurrentPosition());
  info_line->putstr(0, 0, s0.c_str());
}

void
TerminalUI::renderScore(Synth & synth) {
  size_t rows = score_plane->get_dim_y(), cols = score_plane->get_dim_x();
  auto & tracks = synth.getTracks();
  score_plane->set_fg_rgb8(0x80, 0xc0, 0x80);
  for (size_t row = 0; row < rows && row < 32; row++) {
    bool is_current_row = row == synth.getPatternPosition();

    if (is_current_row) {
      score_plane->set_bg_rgb8(0x80, 0xa0, 0x80);
    } else {
      score_plane->set_bg_rgb8(0x00, 0x00, 0x00);
    }
    
    auto s = format("{:02x}|", row);
    score_plane->putstr(row, 0, s.c_str());
    
    for (size_t i = 0; i < tracks.size(); i++) {
      auto & track = tracks[i];
      size_t pi = track.getPattern(synth.getTrackPosition());
      int note = 0;
      if (pi != 255) {
	auto & pattern = synth.getPattern(pi);
	note = pattern.getNote(row);
      }

      if (note != 0) {
	bool has_accent = note & 0x80;
	note &= 0x7f;
	
	auto it = midi_note_names.find(note);
	string s2 = it != midi_note_names.end() ? it->second + " " : format("x{:02x} ", note);
	score_plane->putstr(row, 3 + i*4, s2.c_str());
      } else {
	score_plane->putstr(row, 3 + i*4, "... ");
      }
    }
  }
}

void
TerminalUI::start(Synth & synth, AudioAPI & audio) {
  size_t num_descriptors = 1 + audio.getPollDescriptors().size();
  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);
  
  descriptors[0].fd = 0;
  descriptors[0].events = POLLIN;

  for (size_t i = 0; i < audio.getPollDescriptors().size(); i++) {
    descriptors[1 + i] = audio.getPollDescriptors()[i];
  }
    
  // setStatus("Starting... nd = " + to_string(num_descriptors));

  time_t prev_update = 0;
  time_t prev_pos = synth.getCurrentPosition();

  renderScore(synth);
  
  while ( !close_ui ) {
    bool render = false;
    
    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, 10) > 0) {
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
	    // setStatus(to_string(data.size()) + " " + to_string(waiting_data.size()));
	    
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
	      
	      render = true;
	    }
	  }
	}
      }

      if (prev_pos != synth.getCurrentPosition()) {
	renderInfo(synth);
	prev_pos = synth.getCurrentPosition();
	renderScore(synth);
	render = true;
      }
      
      if (render) {
	nc->render();
      }
    }
  }  
}
