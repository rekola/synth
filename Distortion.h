#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Effect.h"

enum class DistortionType { HARD_CLIP = 1, SOFT_CLIP, BITCRUSH, TANH };

class Distortion : public Effect {
 public:
  Distortion() : type(DistortionType::HARD_CLIP), param(1.0f), drymix(0) { }
  Distortion(DistortionType _type, float _param, float _drymix) : type(_type), param(_param), drymix(_drymix) { }

  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, unsigned int outSamplerate) const override;
  std::string getElementName() const override { return "distortion"; }
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;

 private:
  DistortionType type;
  float param, drymix;
  float drive = 1.0f;
};

#endif
