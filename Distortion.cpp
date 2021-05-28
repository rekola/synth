#include "Distortion.h"

#include "SampleData.h"
#include "TrackState.h"

#include "tinyxml2.h"

using namespace std;

class DistortionState : public TrackState {
public:
  DistortionState(unsigned int outSampleRate, DistortionType _type, float _param, float _drymix)
    : TrackState(outSampleRate), type(_type), param(_param), drymix(_drymix) { }
  
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

    case DistortionType::TANH:
      {
	float timbre = 1.0f;
	float depth = 1.0f;
	float timbreInverse = (1 - (timbre * 0.099)) * 10;
	for (size_t i = 0; i < input.getChannels() * input.size(); i++) {
	  float x = buffer[i];
	  x *= depth;
	  x = tanhf(x * (timbre + 1));
	  x = x * ((0.1 + timbre) * timbreInverse);
	  x = cos((x + (timbre + 0.25)));
	  x = tanh(x * (timbre + 1));                  
	  x = x * 0.125;                      
	  buffer[i] = x;
      	}
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

std::unique_ptr<TrackState>
Distortion::createState(unsigned int outSampleRate) const {
  return make_unique<DistortionState>(outSampleRate, type, param, drymix);
}

void
Distortion::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);
  
  auto param_text = element.Attribute("param");
  param = param_text ? atof(param_text) : 1.0f;

  auto type_text = element.Attribute("type");
  if (type_text) {
    if (strcmp(type_text, "clip") == 0) type = DistortionType::CLIP;
    else if (strcmp(type_text, "tanh") == 0) type = DistortionType::TANH;
  }
}

void
Distortion::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);  
}
