#include "NoteMultiplier.h"

#include <cassert>

using namespace std;

static inline float getRandF() { return (float)rand() / RAND_MAX; }

std::unique_ptr<TrackState>
NoteMultiplier::playNote(const ChannelConfiguration & channel_config, float input_azimuth, float frequency, float input_detune, float velocity, float start_phase, int note_value) const {
  float half_detune_ratio = powf(2, detune_ / 1200 / 2);
    
  auto group = createState(channel_config);
  int voice_id = 0;
  for (auto & child : getChildren()) {
    if (unisons_ == 1) {
      // root
      auto voice = child->playNote(channel_config, input_azimuth, frequency, input_detune, velocity, start_phase + getRandF(), note_value);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    } else if (unisons_ >= 2) {
      float azimuth = input_azimuth - spread_ / 2;
      float azimuth_step = spread_ / (unisons_ - 1);

      float detune = input_detune / half_detune_ratio;
      float detune_step = powf(half_detune_ratio * half_detune_ratio, 1.0f / (unisons_ - 1));
            
      // unisons_
      for (int i = 0; i < unisons_; i++, azimuth += azimuth_step, detune *= detune_step) {
	auto voice = child->playNote(channel_config, azimuth, frequency, detune, velocity, start_phase + getRandF(), note_value);
	if (voice.get()) group->addChild(voice_id++, move(voice));
      }
    }

    // fourths
    for (int i = 0; i < fourths_; i++) {
      float detune = input_detune * powf(4.0f / 3.0f, i + 1) * (1 + getRandF() * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_azimuth, frequency, detune, v, start_phase + getRandF(), note_value);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }

    // fifths
    for (int i = 0; i < fifths_; i++) {
      float detune = input_detune * powf(3.0f / 2.0f, i + 1) * (1 + getRandF() * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_azimuth, frequency, detune, v, start_phase + getRandF(), note_value);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }

    // octaves
    for (int i = 0; i < octaves_; i++) {
      float detune = input_detune * powf(2.0f, i + 1) * (1 + getRandF() * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_azimuth, frequency, detune, v, start_phase + getRandF(), note_value);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }
  }
  return group;
}

void
NoteMultiplier::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  unisons_ = input.getInt("unisons", 1);
  fourths_ = input.getInt("fourths");
  fifths_ = input.getInt("fifths");
  octaves_ = input.getInt("octaves");
  detune_ = input.getFloat("detune");
  spread_ = input.getFloat("spread");
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
