#ifndef _CHORUSBUSEFFECT_H_
#define _CHORUSBUSEFFECT_H_

#include "BusEffect.h"
#include "../dsp/ChorusEngine.h"
#include "../SampleData.h"

// Thin BusEffect adapter around dsp::ChorusEngine for the shared send
// bus's SendB path - ChorusEngine itself stays independent of BusEffect
// (it's genuinely reusable DSP, also used directly by the per-track
// <chorus> effect on an already-positioned signal via its own in-place
// process(SampleData&)) so dsp/ never depends on bus/. Duplicates the
// mono SendB sum into 2 identical channels, then lets the wrapped
// engine's decorrelate = true mode synthesize stereo width from there -
// moving the duplication SendBusProcessor used to do by hand into this
// adapter instead.
class ChorusBusEffect : public BusEffect {
 public:
  explicit ChorusBusEffect(int sampleRate)
    : BusEffect(sampleRate), engine_(2, sampleRate, /*voices=*/3, /*rateHz=*/0.5f, /*centerDelayMs=*/15.0f, /*depthMs=*/4.0f, /*decorrelate=*/true) {
    engine_.setMix(1.0f); // fully wet - dry/wet balance is controlled
                          // upstream by each track's own sendB amount.
  }

  void process(const float * monoInput, int frames) override {
    if (stereo_.numberOfFrames() != frames) stereo_ = SampleData(2, frames);
    auto l = stereo_.getChannelData(0), r = stereo_.getChannelData(1);
    for (int i = 0; i < frames; i++) {
      l[i] = monoInput[i];
      r[i] = monoInput[i];
    }
    engine_.process(stereo_);
  }

  const SampleData & getStereoOutput() const { return stereo_; }

 private:
  ChorusEngine engine_;
  SampleData stereo_;
};

#endif
