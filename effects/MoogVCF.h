#ifndef _MOOGVCF_H_
#define _MOOGVCF_H_

template <class T>
class MoogVCF {  
public:
  MoogVCF() { }
  
  void apply(size_t blockSamples, float * buffer, float cut, float res, bool is_highpass) {
    for (size_t i = 0; i < blockSamples; i++) {
      T input = buffer[i];
      T si = input;
      T f = cut * 1.16;
      T ff = f * f;
      T fb = res * (1.0 - 0.15 * ff);
      f = 1 - f;
      
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
      
      if (is_highpass) buffer[i] = si - out4;
      else buffer[i] = out4;
    }
  }

private:
  T in1 = 0, in2 = 0, in3 = 0, in4 = 0;
  T out1 = 0, out2 = 0, out3 = 0, out4 = 0;
};

#endif
