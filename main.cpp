#include "AlsaAudio.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "StderrLogger.h"

#include <iostream>
#include <cstring>
#include <signal.h>

#include <ncpp/NotCurses.hh>

using namespace std;

int main(int argc, char *argv[]) {
  int load_demo = 0;
  int samplerate = 44100;
  bool relative = false;
  ChannelConfiguration channel_config = ChannelConfiguration::STEREO;
  vector<string> input;
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--demo") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
	i++;
	load_demo = atoi(argv[i]);
      } else {
	load_demo = 1;
      }
    } else if (strcmp(argv[i], "--samplerate") == 0) {
      samplerate = 0;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
	i++;
	samplerate = atoi(argv[i]);	
      }
      if (!samplerate) {
	cerr << "invalid parameters for samplerate\n";
	exit(1);
      }
    } else if (strcmp(argv[i], "--relative") == 0) {
      relative = true;
    } else if (strcmp(argv[i], "--mono") == 0) {
      channel_config = ChannelConfiguration::MONO;
    } else if (strcmp(argv[i], "--stereo") == 0) {
      channel_config = ChannelConfiguration::STEREO;
    } else if (strcmp(argv[i], "--surround") == 0) {
      channel_config = ChannelConfiguration::SURROUND_5_1;
    } else if (argv[i][0] == '-') {
      cerr << "invalid parameter\n";
      exit(1);
    } else {
      input.push_back(argv[i]);
    }
  }
      
  auto controller = make_shared<Controller>(channel_config);

  if (!input.empty()) {
    if (!controller->openSong(input.front())) {
      cerr << "Could not find file " << input.front() << endl;
      exit(1);
    }
  } else if (load_demo == 2) {
    controller->loadDemo2();
  } else {
    controller->createNewSong();
  }

  StderrLogger logger;
  
  AlsaAudio audio(samplerate, channel_config == ChannelConfiguration::MONO ? 1 : 2);
  audio.initialize(logger);

#if 0
  if (!setlocale(LC_ALL, "")) {
    fprintf(stderr, "Couldn't set locale\n");
    exit(1);
  }
#endif
   
  // notcurses_options nopts{};
  // nopts.flags = NCOPTION_INHIBIT_SETLOCALE;
  auto nc = make_shared<ncpp::NotCurses>();
  // nc->mouse_enable(NCMICE_ALL_EVENTS);
  nc->linesigs_disable();
  
  TerminalUI ui(nc);
  ui.initialize(controller);
  ui.start(audio);

  return 0;
}
