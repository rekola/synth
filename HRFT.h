#ifndef _HRFT_H_
#define _HRFT_H_

#include "Mixer.h"

#include <memory>

class CAmbisonicBinauralizer;
class CAmbisonicEncoder;
class CBFormat;

class HRFT : public Mixer {
 public:
  HRFT(unsigned int _outSampleRate);
  
  void reset() override;
  void accumulate(const SampleData & data, float volume, float distance, float azimuth, float elevation) override;
  SampleData encode(float master_volume) override;
  
private:
  std::shared_ptr<CAmbisonicEncoder> myEncoder;
  std::shared_ptr<CAmbisonicBinauralizer> myBinauralizer;
  std::shared_ptr<CBFormat> myBFormat;
  std::unique_ptr<float[]> left_buffer, right_buffer;

  size_t frames = 1024;
};

#endif
