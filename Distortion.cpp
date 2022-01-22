#include "Distortion.h"

#include "SampleData.h"
#include "TrackState.h"

#include "tinyxml2.h"
#include <iostream>

using namespace std;

class DistortionState : public TrackState {
public:
  DistortionState(unsigned int outSampleRate, DistortionType _type, float _param, float _drymix, float _drive)
    : TrackState(outSampleRate), type(_type), param(_param), drymix(_drymix), drive(_drive) { }
  
  void apply(SampleData & input) override {    
    auto buffer = input.data();
    switch (type) {
    case DistortionType::HARD_CLIP:

      for (size_t i = 0; i < input.getChannels() * input.size(); i++) {
	float x = buffer[i];
	float y = drive * x;
	if (y > param) y = param;
	if (y < -param) y = -param;
	buffer[i] = drymix * x + (1.0f - drymix) * y;
      }
      break;

    case DistortionType::SOFT_CLIP:
      for (size_t i = 0; i < input.getChannels() * input.size(); i++) {
	float x = buffer[i];
	float y = drive * x;
	if (y > 1.0) y = 1.0;
	else if (y < -1.0) y = -1.0;
	y = y - y*y*y/3.0f;
	y = 1.5 * y - 0.5 * y*y*y;
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
  float param, drymix, drive;
};

std::unique_ptr<TrackState>
Distortion::createState(unsigned int outSampleRate) const {
  return make_unique<DistortionState>(outSampleRate, type, param, drymix, drive);
}

void
Distortion::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);
  
  auto param_text = element.Attribute("param");
  param = param_text ? strtof(param_text, nullptr) : 1.0f;

  auto drive_text = element.Attribute("drive");
  drive = drive_text ? strtof(drive_text, nullptr) : 1.0f;

  auto type_text = element.Attribute("type");
  if (type_text) {
    if (strcmp(type_text, "hardclip") == 0) type = DistortionType::HARD_CLIP;
    else if (strcmp(type_text, "softclip") == 0) type = DistortionType::SOFT_CLIP;
    else if (strcmp(type_text, "bitchrush") == 0) type = DistortionType::BITCRUSH;
  }
}

void
Distortion::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);  

  element.SetAttribute("param", param);
  element.SetAttribute("drive", drive);

  switch (type) {
  case DistortionType::HARD_CLIP:
    element.SetAttribute("type", "hardclip");
    break;

  case DistortionType::SOFT_CLIP:
    element.SetAttribute("type", "softclip");    
    break;
    
  case DistortionType::BITCRUSH:
    element.SetAttribute("type", "bitcrush");
    break;

  case DistortionType::TANH:
    element.SetAttribute("type", "tanh");
    break;
  }
}
