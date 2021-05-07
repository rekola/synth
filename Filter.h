#ifndef _FILTER_H_
#define _FILTER_H_

#include "Effect.h"
#include "LFO.h"

class Filter : public Effect {
 public:
  Filter(float _fcut, float _fres, bool is_highpass) : fcut(_fcut), fres(_fres), is_highpass(is_highpass) { }

  std::unique_ptr<EffectState> createState(unsigned int samplerate) const override;

  float get_fcut() const { return fcut; }
  float get_fres() const { return fres; }
  bool get_is_highpass() const { return is_highpass; }

private:
  float fcut, fres;
  bool is_highpass;
  // float lfo_amount = 0, lfo_rate = 0, lfo_phase = 0;
  LFO lfo;
};

#endif
