#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Effect.h"

enum class DistortionType { CLIP = 1, BITCRUSH };

class Distortion : public Effect {
 public:
  Distortion(DistortionType _type, float _param, float _drymix) : type(_type), param(_param), drymix(_drymix) { }

  std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const override;

 private:
  DistortionType type;
  float param, drymix;
};

#endif
