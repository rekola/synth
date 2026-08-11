#ifndef _ENVELOPEFILTER_H_
#define _ENVELOPEFILTER_H_

#include "Effect.h"
#include "../Envelope.h"

class EnvelopeFilter : public Effect {
 public:
  EnvelopeFilter() { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "envelope"; }

  void loadParameters(const ParameterSource & input) override {
    Effect::loadParameters(input);
    envelope_.loadParameters(input);
  }

  void storeParameters(ParameterSource & output) const override {
    Effect::storeParameters(output);
    envelope_.storeParameters(output);
  }

 private:
  Envelope envelope_;
};

#endif
