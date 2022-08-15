#ifndef _REVERB_H_
#define _REVERB_H_

#include "Track.h"

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

class Reverb : public Track {
 public:
  explicit Reverb(ReverbPreset _preset = ReverbPreset::SUBTLE) : Track(TrackType::EFFECT), preset(_preset) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::string getElementName() const override { return "reverb"; }
  void loadParameters(const ParameterSource & element) override;
  void storeParameters(ParameterSource & element) const override;

private:
  ReverbPreset preset;
};

#endif
