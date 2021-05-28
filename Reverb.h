#ifndef _REVERB_H_
#define _REVERB_H_

#include "Effect.h"

enum class ReverbPreset { SUBTLE = 0, STADIUM, CUPBOARD, DARK, HALVES };

class Reverb : public Effect {
 public:
  explicit Reverb(ReverbPreset _preset = ReverbPreset::SUBTLE) : preset(_preset) { }

  std::unique_ptr<TrackState> createState(unsigned int outSamplerate) const override;
  std::string getElementName() const override { return "reverb"; }
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;

private:
  ReverbPreset preset;
};

#endif
