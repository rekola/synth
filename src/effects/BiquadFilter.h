#ifndef _BIQUADFILTER_H_
#define _BIQUADFILTER_H_

#include "Effect.h"
#include "../instruments/Envelope.h"
#include "../dsp/FilterType.h"

class BiquadFilter : public Effect {
 public:
  BiquadFilter() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & config) const override;
  const char * getElementName() const override { return "biquadFilter"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

private:
  FilterType type_ { FilterType::lowpass };
  float fc_ = 0.0f, Q_ = 0.0f, peakGainDB_ = 0.0f;
  bool use_aftertouch_ = false;
  Envelope envelope_;
};

#endif
