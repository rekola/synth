
#ifndef _ENVELOPE_H_
#define _ENVELOPE_H_

#include "../state/ParameterSource.h"

class Envelope {
 public:
  Envelope() { }
  Envelope(float _a, float _d, float _s, float _r)
    : delay_(0.0f), attack_(_a), hold_(0.0f), decay_(_d), sustain_(_s), release_(_r) { }

  float getDelay() const { return delay_; }
  float getAttack() const { return attack_; }
  float getDecay() const { return decay_; }
  float getSustain() const { return sustain_; }
  float getRelease() const { return release_; }

  void loadParameters(const ParameterSource & input) {
    attack_ = input.getFloat("attack", 0.0f);
    hold_ = input.getFloat("hold", 0.0f);
    decay_ = input.getFloat("decay", 0.0f);
    sustain_ = input.getFloat("sustain", 1.0f);
    release_ = input.getFloat("release", 0.0f);
    keynumToHold_ = input.getFloat("keynumToHold", 0.0f);
    keynumToDecay_ = input.getFloat("keynumToDecay", 0.0f);
  }

  void storeParameters(ParameterSource & output) const {
    output.set("attack", attack_);
    output.set("hold", hold_);
    output.set("decay", decay_);
    output.set("sustain", sustain_);
    output.set("release", release_);
    output.set("keynumToHold", keynumToHold_);
    output.set("keynumToDecay", keynumToDecay_);
  }

  float delay_ = 0.0f, attack_ = 0.0f, hold_ = 0.0f, decay_ = 0.0f, sustain_ = 0.0f, release_ = 0.0f;
  float keynumToHold_ = 0.0f, keynumToDecay_ = 0.0f;
};

#endif
