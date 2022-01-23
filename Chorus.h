#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Effect.h"

class Chorus : public Effect {
 public:
  Chorus(float _delay1 = 0.0f, float _delay2 = 0.0f) : delay1(_delay1), delay2(_delay2) { }

  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, unsigned int outSamplerate) const override;
  std::string getElementName() const override { return "chorus"; }
  void readXML(tinyxml2::XMLElement & element) override;
  void populateXML(tinyxml2::XMLElement & element) const override;

 private:
  float delay1, delay2; // ms
};

#endif
