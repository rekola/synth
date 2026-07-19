#include "HarmonicSeries.h"

#include <cassert>

using namespace std;

static inline float getRandF() { return (float)rand() / RAND_MAX; }

std::unique_ptr<TrackState>
HarmonicSeries::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & input_position, float frequency, float input_detune, float input_velocity, float start_phase, int note_value) const {
  float half_detune_ratio = powf(2, detune_ / 1200 / 2);
  int child_id = -1;

  auto group = createState(channel_config);
  for (auto & child : getChildren()) {
    for (int i = 1; i <= voices_; i++) {
      if (i + from_ - 1 == skip_) continue;

      auto position = input_position;
      auto velocity = input_velocity * pow(0.75, i - 1);
      float detune = 0;
      if (undertone_) {
#if 0
	if (i == 1 || i == 2) detune = i;
	else if (i == 3) detune = 8.0f/3.0f;
	else if (i == 4) detune = 4;
	else if (i == 5) detune = 14.0f/3.0f;
	else if (i == 6) detune = 16.0f/3.0f;
	else if (i == 7) detune = 20.0f/3.0f;
	else if (i == 8) detune = 8.0f;
	else detune = i;
#else
	if (i == 2 || i == 3 || i == 5) continue;
	detune = i + from_ - 1;
#endif	
      } else {
	detune = i + from_ - 1;
      }
      auto voice = child->playNote(channel_config, position, frequency, detune, velocity, start_phase - getRandF(), note_value);
      if (voice.get()) group->addChild(child_id--, move(voice));
    }
  }
  return group;
}

void
HarmonicSeries::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  voices_ = input.getInt("voices", 256);
  from_ = input.getInt("from", 1);
  skip_ = input.getInt("skip", 0);
  undertone_ = input.getBool("undertone");
  detune_ = input.getFloat("detune");
  spread_ = input.getFloat("spread");
}

void
HarmonicSeries::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("voices", voices_);
  output.set("from", from_);
  output.set("undertone", undertone_);
  output.set("detune", detune_);
  output.set("spread", spread_);
}
