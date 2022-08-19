#include "NoteMultiplier.h"

#include <cassert>

using namespace std;

static inline float getRandF() { return (float)rand() / RAND_MAX; }

std::unique_ptr<TrackState>
NoteMultiplier::playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float input_detune, float velocity, float start_phase) const {
  auto group = createState(channel_config);
  for (auto & child : getChildren()) {
    float unison_velocity = velocity; // / (unisons_ + 1);
    
    // root
    auto voice = child->playNote(channel_config, azimuth, frequency, input_detune, unison_velocity, start_phase + getRandF());
    if (voice.get()) group->addChild(move(voice));

    float detune_start = input_detune * (1 - detune_ / 2);
    float detune_end = input_detune * (1 + detune_ / 2);
    float azimuth_start = azimuth - spread_ / 2;

    if (unisons_ >= 2) {
      float detune_step = powf(detune_end / detune_start, 1.0f / (unisons_ - 1));
      float azimuth_step = spread_ / (unisons_ - 1);
      
      // unisons_
      for (int i = 0; i < unisons_; i++) {
	auto voice = child->playNote(channel_config, azimuth_start + i * azimuth_step, frequency, detune_start * powf(detune_step, i), unison_velocity, start_phase + getRandF());
	if (voice.get()) group->addChild(move(voice));
      }
    }

    int i;
    float m;

    // octaves
    for (i = 0, m = 2; i < octaves_; i++, m *= 2) {
      float detune = input_detune * m * getRandF() * (detune_ - 0.5 * detune_);
      auto voice = child->playNote(channel_config, azimuth, frequency, detune, velocity / m, start_phase + getRandF());
      if (voice.get()) group->addChild(move(voice));
    }

    // fifths
    for (i = 0, m = 3.0f / 2.0f; i < fifths_; i++, m *= 3.0f / 2.0f) {
      float detune = input_detune * m * getRandF() * (detune_ - 0.5 * detune_);
      auto voice = child->playNote(channel_config, azimuth, frequency, detune, velocity / m, start_phase + getRandF());
      if (voice.get()) group->addChild(move(voice));
    }

    for (i = 0, m = 4.0f / 3.0f; i < fourths_; i++, m *= 4.0f / 3.0f) {
      float detune = input_detune * m * getRandF() * (detune_ - 0.5 * detune_);
      auto voice = child->playNote(channel_config, azimuth, frequency, detune, velocity / m, start_phase + getRandF());
      if (voice.get()) group->addChild(move(voice));
    }
}
  return group;
}

void
NoteMultiplier::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  unisons_ = input.getInt("unisons");
  octaves_ = input.getInt("octaves");
  fifths_ = input.getInt("fifths");
  fourths_ = input.getInt("fourths");
  detune_ = input.getFloat("detune");
  spread_ = input.getFloat("spread");

  if (unisons_ & 1) unisons_--;
  if (octaves_ & 1) octaves_--;
  if (fifths_ & 1) fifths_--;
  if (fourths_ & 1) fourths_--;
}

void
NoteMultiplier::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("unisons", unisons_);
  output.set("octaves", octaves_);
  output.set("fifths", fifths_);
  output.set("fourths", fourths_);
  output.set("detune", detune_);
  output.set("spread", spread_);
}
