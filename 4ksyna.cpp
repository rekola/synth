/*
  TODO:
  -modulation
  -note slide
  -better exponential(?) ADSR
  -optimization
*/

#include "Synth.h"
#include "AlsaAudio.h"
#include "track.h"

using namespace std;

int main(int argc, char *argv[]) {
  AlsaAudio audio(44100, 2);
  
  Synth synth(audio.getFrequency(), tr);
  audio.start(synth);
  
  return 0;
}
