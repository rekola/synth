#include "AlsaAudio.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "StderrLogger.h"

#include <iostream>
#include <cstring>

using namespace std;

int main(int argc, char *argv[]) {
  int load_demo = 0;
  int samplerate = 44100;
  bool relative = false;
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
    } else if (argv[i][0] == '-') {
      cerr << "invalid parameter\n";
      exit(1);
    } else {
      input.push_back(argv[i]);
    }
  }
      
  auto controller = make_shared<Controller>();

  if (!input.empty()) {
    if (!controller->openSong(input.front())) {
      cerr << "Could not find file " << input.front() << endl;
      exit(1);
    }
  } else if (load_demo == 1) {
    controller->loadDemo();
  } else if (load_demo == 2) {
    controller->loadDemo2();
  } else if (load_demo == 7) {
    controller->loadDemo7();
  } else {
    controller->createNewSong();
  }

  StderrLogger logger;
  
  AlsaAudio audio(samplerate, 2);
  audio.initialize(logger);
  
  TerminalUI ui;
  ui.initialize(controller);
  ui.start(audio);

  return 0;
}
