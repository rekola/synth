#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Effect.h"

enum class DistortionType { CLIP = 1, TANH, BITCRUSH };

class Distortion : public Effect {
 public:
  Distortion() : type(DistortionType::CLIP), param(1.0f), drymix(0) { }
  Distortion(DistortionType _type, float _param, float _drymix) : type(_type), param(_param), drymix(_drymix) { }

  std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const override;
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;

 private:
  DistortionType type;
  float param, drymix;
};

#endif
