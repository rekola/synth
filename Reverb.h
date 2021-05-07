#ifndef _REVERB_H_
#define _REVERB_H_

#include "Effect.h"

enum class ReverbPreset { SUBTLE = 0, STADIUM, CUPBOARD, DARK, HALVES };

class Reverb : public Effect {
 public:
  explicit Reverb(ReverbPreset _preset) : preset(_preset) { }

  std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const override;

private:
  ReverbPreset preset;
};

#endif
