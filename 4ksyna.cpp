/*
  TODO:
  -modulation
  -note slide
  -better exponential(?) ADSR
  -optimization
*/

#include "Synth.h"
#include "AlsaAudio.h"
#include "UI.h"

#include "track.h"

using namespace std;

int main(int argc, char *argv[]) {
  UI ui;
  ui.initialize();
  
  AlsaAudio audio(44100, 2);
  audio.initialize(ui);
  
  Synth synth(audio.getFrequency(), tr);
  
  ui.start(synth, audio);

  return 0;
}
