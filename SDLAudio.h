#ifndef _SDLAudio_H_
#define _SDLAudio_H_

#include "AudioAPI.h"

class SDLAudio : public AudioAPI {
 public:
  SDLAudio(int _freq, int _channels) : AudioAPI(_freq, _channels) { }

  void start(Synth & synth) override;
};

#endif
