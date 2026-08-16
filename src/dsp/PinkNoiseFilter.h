#ifndef _PINKNOISEFILTER_H_
#define _PINKNOISEFILTER_H_

// Paul Kellett's "refined" pink noise filter (a widely published, public-
// domain approximation - see e.g. musicdsp.org's "generating pink noise"
// entry): a bank of 7 one-pole filters summed together, approximating a
// -3dB/octave (1/f) spectrum from a white noise input. Stateful - each
// instance must keep its own filter state across calls, which is why
// NoiseVoice's NoiseStream (Noise.cpp) owns its own instance rather than
// sharing one across every simultaneously-active NoiseVoice - each note
// gets its own independently-colored noise, not a shared filter state
// that would smear multiple notes' noise together.
class PinkNoiseFilter {
 public:
  float process(float white) {
    b0_ = 0.99886f * b0_ + white * 0.0555179f;
    b1_ = 0.99332f * b1_ + white * 0.0750759f;
    b2_ = 0.96900f * b2_ + white * 0.1538520f;
    b3_ = 0.86650f * b3_ + white * 0.3104856f;
    b4_ = 0.55000f * b4_ + white * 0.5329522f;
    b5_ = -0.7616f * b5_ - white * 0.0168980f;
    float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
    b6_ = white * 0.115926f;
    return pink * 0.11f; // roughly normalizes peak amplitude back to +-1
  }

 private:
  float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f, b3_ = 0.0f, b4_ = 0.0f, b5_ = 0.0f, b6_ = 0.0f;
};

#endif
