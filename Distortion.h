#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Effect.h"

class Distortion : public Effect {
 public:
  enum DistortionType { CLIP = 1, BITCRUSH };
  
 Distortion(DistortionType _type, float _param) : type(_type), param(_param) { }

  void apply(SampleData & input) override {
    auto buffer = input.data();
    for (size_t i = 0; i < input.getChannels() * input.size(); i++) {
      float v = buffer[i];
      if (v > param) buffer[i] = param;
      if (v < -param) buffer[i] = -param;      
    }
  }
  
 private:
  DistortionType type;
  float param;
};

#endif
