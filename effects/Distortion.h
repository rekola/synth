#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Effect.h"

enum class DistortionType { HARD_CLIP = 1, SOFT_CLIP, BITCRUSH, TANH };

static inline const std::string to_string(DistortionType type) {
  switch (type) {
  case DistortionType::HARD_CLIP: return "hardclip";
  case DistortionType::SOFT_CLIP: return "softclip";
  case DistortionType::BITCRUSH: return "bitcrush";
  case DistortionType::TANH: return "tanh";
  default: return "";
  }
}

class Distortion : public Effect {
 public:
  Distortion() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "distortion"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  DistortionType type_ { DistortionType::HARD_CLIP };
  float param_ = 1.0f, drymix_ = 0.0f;
  float drive_ = 1.0f;
};

#endif
