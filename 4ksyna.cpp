/*
  TODO:
  -modulation
  -note slide
  -better exponential(?) ADSR
  -optimization
*/

#include "Synth.h"
#include "AlsaAudio.h"
#include "TerminalUI.h"

#include "track.h"

using namespace std;

int main(int argc, char *argv[]) {
  TerminalUI ui;
  ui.initialize();
  
  AlsaAudio audio(44100, 2);
  audio.initialize(ui);
  
  auto synth = make_shared<Synth>(audio.getFrequency(), tr);

  ui.setSynth(synth);
  ui.start(audio);

  return 0;
}
