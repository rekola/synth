#ifndef _MIXER_H_
#define _MIXER_H_

#include <cstddef>

class Mixer {
 public:
  Mixer() { }
  virtual ~Mixer() { }

  virtual void reset() = 0;
  virtual void accumulate(const float * input, size_t frames, float volume = 1.0f, float distance = 0.0f, float azimuth = 0.0f, float elevation = 0.0f) = 0;
  virtual void encode(float * output, size_t frames, float master_volume) = 0;
  
 private:
  
};

#endif
