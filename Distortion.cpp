#include "Distortion.h"

using namespace std;

class DistortionState : public EffectState {
public:
  DistortionState(unsigned int outSampleRate, DistortionType _type, float _param, float _drymix)
    : EffectState(outSampleRate), type(_type), param(_param), drymix(_drymix) { }
  
  void apply(SampleData & input) override {
    auto buffer = input.data();
    switch (type) {
    case DistortionType::CLIP:
      for (size_t i = 0; i < input.getChannels() * input.size(); i++) {
	float x = buffer[i];
	float y = x;
	if (y > param) y = param;
	if (y < -param) y = -param;
	buffer[i] = drymix * x + (1.0f - drymix) * y;
      }
      break;
       
    case DistortionType::BITCRUSH:
      break;
    }
  }

private:
  DistortionType type;
  float param, drymix;
};

std::unique_ptr<EffectState>
Distortion::createState(unsigned int outSampleRate) const {
  return make_unique<DistortionState>(outSampleRate, type, param, drymix);
}

