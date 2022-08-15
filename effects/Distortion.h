#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Track.h"

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

class Distortion : public Track {
 public:
  Distortion() : Track(TrackType::EFFECT), type_(DistortionType::HARD_CLIP), param_(1.0f), drymix_(0) { }
  Distortion(DistortionType type, float param, float drymix) : Track(TrackType::EFFECT), type_(type), param_(param), drymix_(drymix) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::string getElementName() const override { return "distortion"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  DistortionType type_;
  float param_, drymix_;
  float drive_ = 1.0f;
};

#endif
