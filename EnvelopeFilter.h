#ifndef _ENVELOPEFILTER_H_
#define _ENVELOPEFILTER_H_

#include "Track.h"
#include "Envelope.h"

class EnvelopeFilter : public Track {
 public:
  EnvelopeFilter() : Track(TrackType::EFFECT) { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::string getElementName() const override { return "envelope"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  Envelope envelope;
};

#endif
