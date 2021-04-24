#include "AlsaAudio.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "Synth.h"

#include <iostream>
#include <cstring>

using namespace std;

int main(int argc, char *argv[]) {
  bool load_demo = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--demo") == 0) load_demo = true;
    else {
      cerr << "invalid parameter\n";
      exit(1);
    }
  }
      
  auto controller = make_shared<Controller>();
  if (load_demo) {
    controller->loadDemo();
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
