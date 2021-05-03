#ifndef _HRFT_H_
#define _HRFT_H_

#include <memory>

class CAmbisonicBinauralizer;
class CAmbisonicEncoder;
class CBFormat;

class HRFT {
 public:
  HRFT() { }
  
  void filterAndMix(const float * input, float * output, size_t frames);

  void setElevation(float e) { elevation = e; }
  void setAzimuth(float a) { azimuth = a; }
  void setDistance(float d) { distance = d; }
  
private:
  float elevation = 0, azimuth = 0, distance = 0;

  bool is_initialized = false;
  std::shared_ptr<CAmbisonicEncoder> myEncoder;
  std::shared_ptr<CAmbisonicBinauralizer> myBinauralizer;
  std::shared_ptr<CBFormat> myBFormat;
  float * buffers[2];
};

#endif
