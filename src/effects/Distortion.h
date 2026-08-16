#ifndef _DISTORTION_H_
#define _DISTORTION_H_

#include "Effect.h"
#include "../ambisonic/AmbisonicEncoding.h"

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
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "distortion"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Each of these waveshapers is a nonlinear, per-channel-independent
  // function - unlike a uniform gain multiply, that does NOT commute with
  // linear FOA encoding (distorting each ambisonic channel independently
  // and decoding gives a different, wrong result vs. distorting the
  // pre-encode mono signal and encoding). Needs real stereo/mono input.
  ChannelConfiguration getChildChannelConfiguration(const ChannelConfiguration & config) const override { return reduceForEffect(config); }

 private:
  DistortionType type_ { DistortionType::HARD_CLIP };
  float param_ = 1.0f, drymix_ = 0.0f;
  float drive_ = 1.0f;
};

#endif
