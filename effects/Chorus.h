#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Effect.h"

class Chorus : public Effect {
 public:
  Chorus(float delay1 = 0.0f, float delay2 = 0.0f) : delay1_(delay1), delay2_(delay2) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "chorus"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float delay1_, delay2_; // ms
};

#endif
