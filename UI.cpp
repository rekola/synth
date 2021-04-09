#include "UI.h"

#include <ncpp/Plane.hh>
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

using namespace ncpp;
using namespace std;

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
  }
  
  std::shared_ptr<Plane> n(nc->get_stdplane());

  int rows, cols;
  nc->get_term_dim(&rows, &cols);
  
  top_line = std::make_shared<Plane>(1, cols, 0, 0);
  top_line->set_fg_rgb8(0xc0, 0x80, 0xc0);
  top_line->set_bg_rgb8(0x20, 0x00, 0x20);
  top_line->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
  top_line->putstr("Music Editor");

  status_line = std::make_shared<Plane>(1, cols - 1, rows - 1, 0);
  status_line->set_fg_rgb8(0x80, 0xc0, 0x80);
  status_line->set_bg_rgb8(0x00, 0x40, 0x00);
  status_line->set_base("", 0, CHANNELS_RGB_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));

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
  }
}

inline double GetFrequencyIntensity(double re, double im) {
  return sqrt((re*re)+(im*im));
}

#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))
#define Amplitude(re,im,len) (GetFrequencyIntensity(re,im)/(len))
#define AmplitudeScaled(re,im,len,scale) ((int)Amplitude(re,im,len)%scale)

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

  while ( !close_ui ) {
    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {

      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (d.fd == 0) {
	    setStatus("input");
	    readInput(synth);
	  } else {
	    auto data = synth.play(audio.getFrameCount());
	    audio.play(data, *this);
	  }
	}
      }            
    }
  }  
}
