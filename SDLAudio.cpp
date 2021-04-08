#include "SDLAudio.h"

#include "Synth.h"

#include <SDL/SDL.h>

static void spcallback(void *data, unsigned char *out, int len) {
  Synth * synth = (Synth *)data;
  synth->play((short *)out, len);
}

void
SDLAudio::start(Synth & synth) {
  SDL_AudioSpec w;
  w.freq = getFrequency();
  w.format = AUDIO_S16SYS;
  w.channels = getChannels();
  w.samples = 1024;
  w.callback = spcallback;
  w.userdata = &synth;

  int q = 0;
  SDL_Event e;
  
  SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER);
  SDL_OpenAudio(&w, NULL);
  
  SDL_SetVideoMode(640, 480, 32, 0);
  
  SDL_PauseAudio(0);
  
  while (!q) {
    while (SDL_PollEvent(&e) > 0) {
      if (e.type == SDL_KEYDOWN) {
	q = 1;
      }
    }
  }
  
  SDL_Quit();
}
