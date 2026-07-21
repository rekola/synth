#include "NoteMultiplier.h"

#include <cassert>

using namespace std;

static inline float getRandF() { return (float)rand() / RAND_MAX; }

std::unique_ptr<TrackState>
NoteMultiplier::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & input_position, float frequency, float input_detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const {
  float half_detune_ratio = powf(2, detune_ / 1200 / 2);

  // No reduction of channel_config here, and no createState() override
  // either - NoteMultiplier's own true output format (whatever it was
  // given) is exactly what its caller expects back. Each sub-voice below
  // is itself a leaf instrument (typically Oscilator), which reduces
  // AMBISONIC to MONO on its own before constructing its voice; the
  // inherited plain TrackState this createState() returns already
  // FOA-encodes each differently-positioned sub-voice individually as soon
  // as it notices their channel count is narrower than its own (see
  // TrackState::render(int frames), AmbisonicEncoding.h) - no group-state
  // override needed here for that to work.
  auto group = createState(channel_config);
  int voice_id = 0;
  for (auto & child : getChildren()) {
    if (unisons_ == 1) {
      // root
      auto voice = child->playNote(channel_config, input_position, frequency, input_detune, velocity, start_phase + getRandF(), note_value, send_a, send_b);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    } else if (unisons_ >= 2) {
      float azimuth_offset = -spread_ / 2;
      float azimuth_step = spread_ / (unisons_ - 1);

      float detune = input_detune / half_detune_ratio;
      float detune_step = powf(half_detune_ratio * half_detune_ratio, 1.0f / (unisons_ - 1));

      // unisons_ - spread across azimuth (as before) and, weighted ~3:1
      // azimuth:elevation, across elevation too, so a wide unison chord
      // doesn't collapse onto one flat horizontal line in ambisonic mode.
      for (int i = 0; i < unisons_; i++, azimuth_offset += azimuth_step, detune *= detune_step) {
	SphericalPosition position = input_position;
	position.azimuth += azimuth_offset;
	position.elevation += azimuth_offset / 3.0f;
	auto voice = child->playNote(channel_config, position, frequency, detune, velocity, start_phase + getRandF(), note_value, send_a, send_b);
	if (voice.get()) group->addChild(voice_id++, move(voice));
      }
    }

    // fourths
    for (int i = 0; i < fourths_; i++) {
      float detune = input_detune * powf(4.0f / 3.0f, i + 1) * (1 + getRandF() * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_position, frequency, detune, v, start_phase + getRandF(), note_value, send_a, send_b);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }

    // fifths
    for (int i = 0; i < fifths_; i++) {
      float detune = input_detune * powf(3.0f / 2.0f, i + 1) * (1 + getRandF() * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_position, frequency, detune, v, start_phase + getRandF(), note_value, send_a, send_b);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }

    // octaves
    for (int i = 0; i < octaves_; i++) {
      float detune = input_detune * powf(2.0f, i + 1) * (1 + getRandF() * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_position, frequency, detune, v, start_phase + getRandF(), note_value, send_a, send_b);
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
