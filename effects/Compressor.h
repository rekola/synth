#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

#include "Effect.h"

class Compressor : public Effect {
 public:
  Compressor() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "compressor"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  
private:
  float pregain_;   // dB, amount to boost the signal before applying compression [0 to 100]
  float threshold_; // dB, level where compression kicks in [-100 to 0]
  float knee_;      // dB, width of the knee [0 to 40]
  float ratio_;     // unitless, amount to inversely scale the output when applying comp [1 to 20]
  float attack_;    // seconds, length of the attack phase [0 to 1]
  float release_;   // seconds, length of the release phase [0 to 1]

  // advanced parameters:
  float predelay_;     // seconds, length of the predelay buffer [0 to 1]
  float releasezone1_; // release zones should be increasing between 0 and 1, and are a fraction
  float releasezone2_; //  of the release time depending on the input dB -- these parameters define
  float releasezone3_; //  the adaptive release curve, which is discussed in further detail in the
  float releasezone4_; //  demo: adaptive-release-curve.html
  float postgain_;     // dB, amount of gain to apply after compression [0 to 100]
  float wet_;          // amount to apply the effect [0 completely dry to 1 completely wet]
};

#endif
