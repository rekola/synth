#include "AlsaAudio.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "Synth.h"

using namespace std;

int main(int argc, char *argv[]) {
  auto controller = make_shared<Controller>();

  TerminalUI ui;
  ui.initialize(controller);
  
  AlsaAudio audio(44100, 2);
  audio.initialize(ui);
  
  auto synth = make_shared<Synth>(audio.getFrequency());
  controller->setSynth(synth);
  
  ui.start(audio);

  return 0;
}
