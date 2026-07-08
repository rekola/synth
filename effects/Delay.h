#ifndef _DELAY_H_
#define _DELAY_H_

#include "Effect.h"

class Delay : public Effect {
 public:
  Delay() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "delay"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float delay_ = 0.0f; // sec or rows
  float feedback_ = 0.0f, delaymix_ = 0.0f;
  bool bpm_lock_ = false;
};

#endif
