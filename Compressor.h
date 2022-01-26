#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

#include "Effect.h"

class Compressor : public Effect {
 public:
  Compressor() { }

  std::unique_ptr<TrackState> createState(ChannelConfiguration channel_config, int outSampleRate) const override;
  std::string getElementName() const override { return "compressor"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  void setTreshold(float thresh) { f_thresh = thresh; }
  void setRatio(float ratio) { f_ratio = ratio >= 1.0f ? ratio : 1; }
  void setAttack(float attack) { f_attack = attack >= 1.0f ? attack : 1; }
  void setRelease(float release) { f_release = release >= 1.0f ? release : 1; }

  float getTreshold() const { return f_thresh; }
  float getRatio() const { return f_ratio; }
  float getAttack() const { return f_attack; }
  float getRelease() const { return f_release; }
  
private:
  float f_thresh = 0.05f;	// The level above which the compressor activates (0.05)
  float f_ratio = 10;		// The input to output ratio of gain reduction
  float f_attack = 50;		// The length in time it takes for the compressor to begin reducing gain after the signal has crossed above the threshold
  float f_release = 50;		// The lenght in time it takes for the compressor to stop reducing gain after the signal has crossed below the threshold
};

#endif
