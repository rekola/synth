#ifndef _ENVELOPE_H_
#define _ENVELOPE_H_

#include "ParameterSource.h"

class Envelope {
 public:
  Envelope() : delay(0.0f), attack(0.0f), hold(0.0f), decay(0.0f), sustain(1.0f), release(0.0f), keynumToHold(0.0f), keynumToDecay(0.0f) { }
  Envelope(float _a, float _d, float _s, float _r)
    : delay(0.0f), attack(_a), hold(0.0f), decay(_d), sustain(_s), release(_r), keynumToHold(0.0f), keynumToDecay(0.0f)
    {
      
    }

  float getDelay() const { return delay; }
  float getAttack() const { return attack; }
  float getDecay() const { return decay; }
  float getSustain() const { return sustain; }
  float getRelease() const { return release; }

  void loadParameters(const ParameterSource & input) {
    attack = input.getFloat("attack", 0.0f);
    hold = input.getFloat("hold", 0.0f);
    decay = input.getFloat("decay", 0.0f);
    sustain = input.getFloat("sustain", 1.0f);
    release = input.getFloat("release", 0.0f);    
  }

  void storeParameters(ParameterSource & output) const {
    // TODO
  }

  float delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay;  
};

#endif
