#ifndef _REVERB_H_
#define _REVERB_H_

#include "Effect.h"

enum class ReverbPreset { SUBTLE = 0, STADIUM, CUPBOARD, DARK, HALVES };

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
  explicit Reverb(ReverbPreset _preset = ReverbPreset::SUBTLE) : preset(_preset) { }

  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, int outSamplerate) const override;
  std::string getElementName() const override { return "reverb"; }
  void loadParameters(const ParameterSource & element) override;
  void storeParameters(ParameterSource & element) const override;

private:
  ReverbPreset preset;
};

#endif
