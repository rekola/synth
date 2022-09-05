#ifndef _BIQUADFILTER_H_
#define _BIQUADFILTER_H_

#include "FilterType.h"

#include <cmath>

template <class T>
class BiquadFilter {
public:
  BiquadFilter(FilterType type) : type_(type) {

  }

  void setup(float Fc) {
    // Biquad filter from http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
    T K = tan(M_PI * Fc), KK = K * K;
    T norm = 1 / (1 + K * QInv + KK);
    a0 = KK * norm;
    a1 = 2 * a0;
    b1 = 2 * (KK - 1) * norm;
    b2 = (1 - K * QInv + KK) * norm;
  }  

  float process(double In) {
    T Out = In * a0 + z1;
    z1 = In * a1 + z2 - b1 * Out;
    z2 = In * a0 - b2 * Out;
    return (float)Out;
  }

  T QInv = 0;
  T a0, a1, b1, b2;
  T z1 = 0.0, z2 = 0.0;
  bool active = false;

private:
  FilterType type_;
};

#endif
