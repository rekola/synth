#ifndef _TREMOLO_H_
#define _TREMOLO_H_

#include "Effect.h"

class Tremolo : public Effect {
 public:
  Tremolo() { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "tremolo"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float frequency_ = 1.0f;
  float amplitude_ = 0.0f;
  bool use_aftertouch_ = false;
};

#endif
