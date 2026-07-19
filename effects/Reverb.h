#ifndef _REVERB_H_
#define _REVERB_H_

#include "Effect.h"
#include "../AmbisonicEncoding.h"

enum class ReverbPreset { NONE = 0, SUBTLE, STADIUM, CUPBOARD, DARK, HALVES };

static inline const std::string to_string(ReverbPreset preset) {
  switch (preset) {
  case ReverbPreset::SUBTLE: return "subtle";
  case ReverbPreset::STADIUM: return "stadium";
  case ReverbPreset::CUPBOARD: return "cupboard";
  case ReverbPreset::DARK: return "dark";
  case ReverbPreset::HALVES: return "halves";
  default: return "";
  }
}

class Reverb : public Effect {
 public:
  explicit Reverb() {
    setVendorName("Martin Eastwood");
  }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "reverb"; }
  void loadParameters(const ParameterSource & element) override;
  void storeParameters(ParameterSource & element) const override;

  // Real stereo-width MVerb processing needs genuine 2-channel input, not
  // raw ambisonic channels - see AmbisonicEncoding.h and the "Effects"
  // section of the spatial audio plan.
  ChannelConfiguration getChildChannelConfiguration(const ChannelConfiguration & config) const override { return reduceForEffect(config); }

private:
  ReverbPreset preset_ { ReverbPreset::NONE };
  float damping_freq_ { 0.9f };
  float density_ { 0.0f };
  float bandwidth_freq_ { 0.9f };
  float decay_ { 0.5f };
  float predelay_ { 0.0f };
  float size_ { 1.0f };
  float gain_ { 1.0f };
  float mix_ { 1.0f };
  float earlymix_ { 1.0f };

  bool bpm_lock_ { false };
};

#endif
