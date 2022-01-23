#ifndef _ENVELOPEFILTER_H_
#define _ENVELOPEFILTER_H_

#include "Effect.h"
#include "Envelope.h"

class EnvelopeFilter : public Effect {
 public:
  EnvelopeFilter() { }
  
  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, unsigned int outSampleRate) const override;
  std::string getElementName() const override { return "envelope"; }
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;

 private:
  Envelope envelope;
};

#endif
