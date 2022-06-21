#include "Oscilator.h"

#include "InstrumentVoice.h"
#include "SampleData.h"

using namespace std;

static inline float create_sine(double i0) {
  float i = i0 - (long long)(i0);
  return sinf(2.0f * float(M_PI) * i);
}

static inline float create_saw(double i0) {
  float i = i0 - (long long)(i0);
  return i < 0.5f ? 2.0f * i : 2.0f * i - 2.0f;
}

static inline float create_triangle(double i0) {
  float i = i0 - (long long)(i0);
  return i < 0.5f ? 1.0f - 4.0f * i : 4.0f * i - 3;
}

static inline float create_square(double i0) {
  float i = i0 - (long long)(i0);
  return i < 0.5f ? -1.0f : 1.0f;
}

static inline float create_noise() {
  return ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
}

class OscilatorVoice : public InstrumentVoice {
public:
  OscilatorVoice(ChannelConfiguration _config, float _azimuth, WaveformType _type, int _harmonic, int _subharmonic, float _level)
    : InstrumentVoice(_config, _azimuth), type(_type), harmonic(_harmonic), subharmonic(_subharmonic), level(_level) {
  }

  SampleData render(int frames) override {
    float gain = decibelsToGain(getGainDB()) * level;

    SampleData data(getChannelConfiguration(), frames);
    auto num_channels = data.numberOfChannels();
    auto buffer = data.data();
    
    double pos = getSourceSamplePosition() / getChannelConfiguration().getAudioOutSampleRate();    
    double rate = getFrequency() / getChannelConfiguration().getAudioOutSampleRate() * harmonic / subharmonic;

    if (num_channels == 2) {
      float pan = sin(getAzimuth() / 180.0f * M_PI) / 2;
      if (pan < -0.5) pan = -0.5;
      else if (pan > 0.5) pan = 0.5;
      float left_f = sqrtf(0.5f - pan) * gain, right_f = sin(0.5f + pan) * gain;

      if (!getChildren().empty() && type != WaveformType::NOISE) {
	auto modulator = InstrumentVoice::render(frames);
	assert(modulator.numberOfChannels() == 1);
	auto modulator_data = modulator.data();
	
	switch (type) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_sine(pos + modulator_data[k]);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    float a = create_saw(pos + modulator_data[2 * k + 0]);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;create_saw(pos + modulator_data[2 * k + 1]);
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_triangle(pos + modulator_data[2 * k + 0]);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_square(pos + modulator_data[2 * k + 0]);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  // noise cannot be modulated
	  break;
	}
      } else {
	switch (type) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_sine(pos);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    float a = create_saw(pos);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_triangle(pos);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_square(pos);
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  for (int k = 0; k < frames; k++) {
	    float a = create_noise();
	    buffer[2 * k + 0] = left_f * a;
	    buffer[2 * k + 1] = right_f * a;
	    pos += rate;
	  }
	  break;
	}
      }
    } else {
      if (!getChildren().empty() && type != WaveformType::NOISE) {
	auto modulator = InstrumentVoice::render(frames);
	auto modulator_data = modulator.data();
	
	switch (type) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {	  
	    buffer[k] = create_sine(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_saw(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_triangle(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_square(pos + modulator_data[k]) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  // noise cannot be modulated
	  break;
	}
      } else {
	switch (type) {
	case WaveformType::SINE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_sine(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SAW:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_saw(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::TRIANGLE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_triangle(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::SQUARE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_square(pos) * gain;
	    pos += rate;
	  }
	  break;
	case WaveformType::NOISE:
	  for (int k = 0; k < frames; k++) {
	    buffer[k] = create_noise() * gain;
	    pos += rate;
	  }
	  break;
	}
      }
    }
    
    stepForward(frames);

    return data;
  }
  
private:
  WaveformType type;
  int harmonic, subharmonic;
  float level;
};

std::unique_ptr<TrackState>
Oscilator::playNote(const ChannelConfiguration & config, float azimuth, float frequency, float velocity, float start_phase) const {  
  auto voice = std::make_unique<OscilatorVoice>(config, azimuth, type, harmonic, subharmonic, level);
  voice->playNote(frequency, velocity, start_phase);

  ChannelConfiguration child_config = config;
  child_config.setType(ChannelConfiguration::MONO);
  
  // don't pass velocity or azimuth to children
  for (auto & child : getChildren()) {    
    auto modulator = child->playNote(child_config, 0.0f, frequency, 1.0, start_phase);
    if (modulator.get()) voice->addChild(move(modulator));
  }
  
  return voice;
}

void
Oscilator::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);
  
  auto type_text = input.getText("type");
  if (type_text == "sine") type = WaveformType::SINE;
  else if (type_text == "saw") type = WaveformType::SAW;
  else if (type_text == "triangle") type = WaveformType::TRIANGLE;
  else if (type_text == "square") type = WaveformType::SQUARE;

  level = input.getFloat("level", 1.0f);
  harmonic = input.getInt("harmonic", 1);
  subharmonic = input.getInt("subharmonic", 1);  
}

void
Oscilator::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);
  output.set("type", to_string(type));
}
