#include "Distortion.h"

#include "SampleData.h"
#include "TrackState.h"

#include <iostream>

using namespace std;

class DistortionState : public TrackState {
public:
  DistortionState(const ChannelConfiguration & _channel_config, DistortionType _type, float _param, float _drymix, float _drive)
    : TrackState(_channel_config), type(_type), param(_param), drymix(_drymix), drive(_drive) { }
  
  SampleData render(int frames) override {
    auto input = TrackState::render(frames);
    
    auto buffer = input.data();
    switch (type) {
    case DistortionType::HARD_CLIP:

      for (size_t i = 0; i < input.numberOfChannels() * input.size(); i++) {
	float x = buffer[i];
	float y = drive * x;
	if (y > param) y = param;
	if (y < -param) y = -param;
	buffer[i] = drymix * x + (1.0f - drymix) * y;
      }
      break;

    case DistortionType::SOFT_CLIP:
      for (size_t i = 0; i < input.numberOfChannels() * input.size(); i++) {
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
	for (size_t i = 0; i < input.numberOfChannels() * input.size(); i++) {
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

    return input;
  }

private:
  DistortionType type;
  float param, drymix, drive;
};

std::unique_ptr<TrackState>
Distortion::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DistortionState>(channel_config, type, param, drymix, drive);
}

void
Distortion::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
   
  param = input.getFloat("param");
  drive = input.getFloat("drive");
  
  auto type_text = input.getText("type");
  if (type_text == "hardclip") type = DistortionType::HARD_CLIP;
  else if (type_text == "softclip") type = DistortionType::SOFT_CLIP;
  else if (type_text == "bitchrush") type = DistortionType::BITCRUSH;  
}

void
Distortion::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("param", param);
  output.set("drive", drive);
  output.set("type", to_string(type));
}
