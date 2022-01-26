#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Effect.h"

class Chorus : public Effect {
 public:
  Chorus(float _delay1 = 0.0f, float _delay2 = 0.0f) : delay1(_delay1), delay2(_delay2) { }

  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, int outSamplerate) const override;
  std::string getElementName() const override { return "chorus"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float delay1, delay2; // ms
};

#endif
