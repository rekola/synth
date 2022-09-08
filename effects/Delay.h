#ifndef _DELAY_H_
#define _DELAY_H_

#include "Effect.h"

class Delay : public Effect {
 public:
  Delay(float delay = 0.0f, float fd = 0.0f, float delaymix = 0.0f)
    : delay_(delay), fd_(fd), delaymix_(delaymix) {
  }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "delay"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float delay_; // sec
  float fd_, delaymix_;
};

#endif
