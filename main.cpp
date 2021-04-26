#include "AlsaAudio.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "Synth.h"

#include <iostream>
#include <cstring>

using namespace std;

int main(int argc, char *argv[]) {
  int load_demo = 0;
  bool relative = false;
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--demo") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
	i++;
	load_demo = atoi(argv[i]);
      } else {
	load_demo = 1;
      }
    } else if (strcmp(argv[i], "--relative") == 0) {
      relative = true;
    } else {
      cerr << "invalid parameter\n";
      exit(1);
    }
  }
      
  auto controller = make_shared<Controller>();
  if (load_demo == 1) {
    controller->loadDemo();
  } else if (load_demo == 2) {
    controller->loadDemo2();
  } else {
    controller->createNewSong();
  }

  TerminalUI ui;
  ui.initialize(controller);
  
  AlsaAudio audio(44100, 2);
  audio.initialize(ui);
  
  auto synth = make_shared<Synth>(audio.getFrequency());
  controller->setSynth(synth);
  
  ui.start(audio);

  return 0;
}
