#ifndef _ALSAAUDIO_H_
#define _ALSAAUDIO_H_

#include "AudioAPI.h"

class AlsaAudio : public AudioAPI {
 public:
 AlsaAudio(int _freq, int _channels) : AudioAPI(_freq, _channels) { }
  
  void start(Synth & synth) override;

};

#endif
