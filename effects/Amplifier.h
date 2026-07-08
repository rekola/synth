#ifndef _AMPLIFIER_H_
#define _AMPLIFIER_H_

#include "Effect.h"

class Amplifier : public Effect {
public:
  Amplifier() { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "amplifier"; }
  void loadParameters(const ParameterSource & element) override;
  void storeParameters(ParameterSource & element) const override;

private:
  float gain_ = 0.0f;
};

#endif
