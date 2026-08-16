#include "NoteMultiplier.h"
#include "AmbisonicEncoding.h"
#include "dsp/HashField.h"

#include <cassert>
#include <cmath>

using namespace std;

namespace {
// Fixed compile-time seed, not per-instance - see InstrumentVoice.h's own
// kNotePhaseSalt for the identical reasoning: the coordinate (specifically
// note_coord.withInstance(voice_id) below, one per generated sub-voice)
// carries the per-voice variation, this salt just keeps NoteMultiplier's
// own detune-jitter axis decorrelated from every other HashField-derived
// value the same note might draw (each leaf's own start phase included -
// see InstrumentVoice.h).
constexpr uint64_t kDetuneSalt = 0x4E6F7465446574ull;
}

std::unique_ptr<VoiceState>
NoteMultiplier::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & input_position, float frequency, float input_detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  float half_detune_ratio = powf(2, detune_ / 1200 / 2);
  HashField detune_field(kDetuneSalt);

  // No reduction of channel_config here, and no createVoiceState() override
  // either - NoteMultiplier's own true output format (whatever it was
  // given) is exactly what its caller expects back. Each sub-voice below
  // is itself a leaf instrument (typically Oscillator), which reduces
  // AMBISONIC to MONO on its own before constructing its voice; the
  // inherited plain VoiceState this createVoiceState() returns already
  // FOA-encodes each differently-positioned sub-voice individually as soon
  // as it notices their channel count is narrower than its own (see
  // VoiceState::render(int frames), AmbisonicEncoding.h) - no group-state
  // override needed here for that to work.
  auto group = createVoiceState(channel_config);
  int voice_id = 0;
  for (auto & child : getChildren()) {
    if (unisons_ == 1) {
      // root - each child's own start phase is derived from its own
      // coordinate (note_coord.withInstance(voice_id), decorrelating it
      // from every other generated sub-voice) internally, by whichever
      // leaf actually constructs a voice (InstrumentVoice's own
      // constructor) - nothing computed or injected here any more.
      auto voice = child->playNote(channel_config, input_position, frequency, input_detune, velocity, note_value, sends, note_coord.withInstance(voice_id));
      if (voice.get()) group->addChild(voice_id++, move(voice));
    } else if (unisons_ >= 2) {
      // spread_ is a dimensionless multiplier on the resolved instrument's
      // own extent (not a raw angle) - the actual angular half-width
      // narrows with distance and widens with extent, same as every other
      // source-attached spread in this codebase (percussion-key offsets,
      // the pitched arc). atan2 rather than atan handles distance <= 0
      // (an untouched/diffuse track - computeAmbisonicGains() ignores
      // azimuth entirely there anyway, so the exact saturated angle atan2
      // picks doesn't matter) without dividing by zero.
      float half_width_deg = atan2f(spread_ * input_position.extent, input_position.distance) * 180.0f / static_cast<float>(M_PI);
      float azimuth_offset = -half_width_deg;
      float azimuth_step = 2.0f * half_width_deg / (unisons_ - 1);

      float detune = input_detune / half_detune_ratio;
      float detune_step = powf(half_detune_ratio * half_detune_ratio, 1.0f / (unisons_ - 1));

      // unisons_ - spread across azimuth (as before) and, weighted by the
      // shared shape ratio, across elevation too, so a wide unison chord
      // doesn't collapse onto one flat horizontal line in ambisonic mode.
      for (int i = 0; i < unisons_; i++, azimuth_offset += azimuth_step, detune *= detune_step) {
	SphericalPosition position = input_position;
	position.azimuth += azimuth_offset;
	position.elevation += azimuth_offset / kExtentShapeRatio;
	auto voice = child->playNote(channel_config, position, frequency, detune, velocity, note_value, sends, note_coord.withInstance(voice_id));
	if (voice.get()) group->addChild(voice_id++, move(voice));
      }
    }

    // fourths
    for (int i = 0; i < fourths_; i++) {
      float detune = input_detune * powf(4.0f / 3.0f, i + 1) * (1 + detune_field.unit(note_coord.withInstance(voice_id).toHashCoord(), paramId("notemul_detune")) * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_position, frequency, detune, v, note_value, sends, note_coord.withInstance(voice_id));
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }

    // fifths
    for (int i = 0; i < fifths_; i++) {
      float detune = input_detune * powf(3.0f / 2.0f, i + 1) * (1 + detune_field.unit(note_coord.withInstance(voice_id).toHashCoord(), paramId("notemul_detune")) * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_position, frequency, detune, v, note_value, sends, note_coord.withInstance(voice_id));
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }

    // octaves
    for (int i = 0; i < octaves_; i++) {
      float detune = input_detune * powf(2.0f, i + 1) * (1 + detune_field.unit(note_coord.withInstance(voice_id).toHashCoord(), paramId("notemul_detune")) * (detune_ - 0.5f * detune_));
      float v = velocity * powf(0.5f, i + 1);

      auto voice = child->playNote(channel_config, input_position, frequency, detune, v, note_value, sends, note_coord.withInstance(voice_id));
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
