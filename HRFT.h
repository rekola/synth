#ifndef _HRFT_H_
#define _HRFT_H_

#include "Mixer.h"

#include <memory>

class CAmbisonicBinauralizer;
class CAmbisonicEncoder;
class CBFormat;

class HRFT : public Mixer {
 public:
  HRFT() { }
  
  void reset() override;
  void accumulate(const float * input, size_t frames, float distance, float azimuth, float elevation) override;
  void encode(float * output, size_t frames, float master_volume) override;
  
private:
  void initialize(size_t frames);
  
  bool is_initialized = false;
  std::shared_ptr<CAmbisonicEncoder> myEncoder;
  std::shared_ptr<CAmbisonicBinauralizer> myBinauralizer;
  std::shared_ptr<CBFormat> myBFormat;
  float * buffers[2];
};

#endif
