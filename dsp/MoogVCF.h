/*
 * CSound source code, Stilson/Smith CCRMA paper., Timo Tossavainen (?) version
 * Type: 24db resonant lowpass
 * Created: 2002-01-17 02:04:57
 * 
 * in[x] and out[x] are member variables, init to 0.0 the controls:
 * 
 * fc = cutoff, nearly linear [0,1] -> [0, fs/2]
 * res = resonance [0, 4] -> [no resonance, self-oscillation]
 */

#ifndef _MOOGVCF_H_
#define _MOOGVCF_H_

template <class T>
class MoogVCF {  
public:
  MoogVCF() { }
  
  void apply(size_t blockSamples, float * buffer, T fc, T res) {
    if (fc < 0) fc = 0;
    if (fc > 1) fc = 1;
    if (res < 0) res = 0;
    if (res > 4) res = 4;

    auto f = fc * 1.16f;
    auto ff = f * f;
    auto fb = res * (1.0f - 0.15f * ff);

    f = 1 - f;

    for (size_t i = 0; i < blockSamples; i++) {
      auto input = buffer[i];
      
      input -= out4 * fb;
      input *= 0.35013f * ff * ff;
      out1 = input + 0.3f * in1 + f * out1; // Pole 1
      in1  = input;
      out2 = out1 + 0.3f * in2 + f * out2;  // Pole 2
      in2 = out1;
      out3 = out2 + 0.3f * in3 + f * out3;  // Pole 3
      in3  = out2;
      out4 = out3 + 0.3f * in4 + f * out4;  // Pole 4
      in4  = out3;
      
      buffer[i] = (float)out4;
    }
  }

  // Silence-only overload: advances the filter's internal state (the four
  // pole in/out histories) as if blockSamples zero samples had been
  // processed, with no buffer needed - for a channel that's absent this
  // block but whose history should keep evolving/decaying rather than
  // freezing and resuming later as if no time had passed.
  void apply(size_t blockSamples, T fc, T res) {
    if (fc < 0) fc = 0;
    if (fc > 1) fc = 1;
    if (res < 0) res = 0;
    if (res > 4) res = 4;

    auto f = fc * 1.16f;
    auto ff = f * f;
    auto fb = res * (1.0f - 0.15f * ff);

    f = 1 - f;

    for (size_t i = 0; i < blockSamples; i++) {
      T input = -out4 * fb;
      input *= 0.35013f * ff * ff;
      out1 = input + 0.3f * in1 + f * out1; // Pole 1
      in1  = input;
      out2 = out1 + 0.3f * in2 + f * out2;  // Pole 2
      in2 = out1;
      out3 = out2 + 0.3f * in3 + f * out3;  // Pole 3
      in3  = out2;
      out4 = out3 + 0.3f * in4 + f * out4;  // Pole 4
      in4  = out3;
    }
  }

private:
  T in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  T out1 = 0, out2 = 0, out3 = 0, out4 = 0;
};

#endif
