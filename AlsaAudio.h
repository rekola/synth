#ifndef _ALSAAUDIO_H_
#define _ALSAAUDIO_H_

#include "AudioAPI.h"

#include <alsa/asoundlib.h>

class AlsaAudio : public AudioAPI {
 public:
  explicit AlsaAudio(int _freq, int _channels) : AudioAPI(_freq, _channels) { }
  ~AlsaAudio();

  void initialize(UIBase & ui);

  size_t getFrameCount() const override { return buffer_size / (2*sizeof(float)); }
  void play(SampleData & data, UIBase & ui) override;

private:
  snd_pcm_t * pcm_handle = 0;
  size_t buffer_size = 0;
};

#endif
