#ifndef _LOWPASSFILTER_H_
#define _LOWPASSFILTER_H_

class LowpassFilter {
public:
  LowpassFilter() {

  }

  void setup(float Fc) {
    // Lowpass filter from http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
    double K = tan(M_PI * Fc), KK = K * K;
    double norm = 1 / (1 + K * QInv + KK);
    a0 = KK * norm;
    a1 = 2 * a0;
    b1 = 2 * (KK - 1) * norm;
    b2 = (1 - K * QInv + KK) * norm;
  }  

  float process(double In) {
    double Out = In * a0 + z1;
    z1 = In * a1 + z2 - b1 * Out;
    z2 = In * a0 - b2 * Out;
    return (float)Out;
  }

  double QInv = 0;
  double a0, a1, b1, b2;
  double z1 = 0.0, z2 = 0.0;
  bool active = false;
};

#endif
