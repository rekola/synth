#ifndef _ENVELOPEFILTER_H_
#define _ENVELOPEFILTER_H_

#include "Effect.h"
#include "Envelope.h"

class EnvelopeFilter : public Effect {
 public:
  EnvelopeFilter() { }
  
  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, int outSampleRate) const override;
  std::string getElementName() const override { return "envelope"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  Envelope envelope;
};

#endif
