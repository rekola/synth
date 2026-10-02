#include "NoteMultiplier.h"
#include "Oscillator.h"
#include "OscillatorArrayVoice.h"
#include "../ambisonic/AmbisonicEncoding.h"
#include "../dsp/HashField.h"

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
NoteMultiplier::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & input_position, Tuning tuning, float input_detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  float half_detune_ratio = powf(2, detune_ / 1200 / 2);
  HashField detune_field(kDetuneSalt);

  // True whenever this call creates more than one simultaneous copy of
  // the same child instrument (Track.h's doc comment on this parameter).
  // ORed with, not replacing, whatever this call already received, so a
  // NoteMultiplier nested inside another still decorrelates when the
  // outer one says so.
  int total_copies = (unisons_ >= 2 ? unisons_ : 1) + fourths_ + fifths_ + octaves_;
  bool decorrelate = needs_decorrelation || total_copies > 1;

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

  // Every Oscillator child's copies share one OscillatorArrayVoice instead
  // of each being its own voice; any other child type still gets a voice
  // per copy. Created on the first Oscillator copy, added to the group
  // after the loop.
  std::unique_ptr<OscillatorArrayVoice> array_voice;

  // One copy of `child` - position/detune/velocity_scale are that copy's own.
  auto spawn = [&](const std::unique_ptr<Track> & child, const SphericalPosition & position, float detune, float velocity_scale) {
    auto coord = note_coord.withInstance(voice_id);
    if (auto * osc = dynamic_cast<const Oscillator *>(child.get())) {
      if (!array_voice) array_voice = std::make_unique<OscillatorArrayVoice>(channel_config, input_position, sends, note_coord);
      array_voice->addCopy({ osc->getType(), osc->getLevel(), osc->getPulseWidth(), osc->applyHarmonics(detune), velocity_scale, position, coord });
      voice_id++;
    } else {
      auto voice = child->playNote(channel_config, position, tuning, detune, velocity * velocity_scale, note_value, sends, coord, decorrelate);
      if (voice.get()) group->addChild(voice_id++, move(voice));
    }
  };

  for (auto & child : getChildren()) {
    if (unisons_ == 1) {
      // root - each copy's own start phase is derived from its own
      // coordinate (note_coord.withInstance(voice_id), decorrelating it
      // from every other generated sub-voice) internally, by whichever
      // leaf actually constructs a voice - nothing computed or injected
      // here.
      spawn(child, input_position, input_detune, 1.0f);
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
	spawn(child, position, detune, 1.0f);
      }
    }

    // fourths, fifths, octaves - each i-th copy an interval higher and
    // half as loud again, jittered by up to detune_ cents.
    auto stacked = [&](int count, float interval) {
      for (int i = 0; i < count; i++) {
	float cents_jitter = detune_field.bipolar(note_coord.withInstance(voice_id).toHashCoord(), paramId("notemul_detune"), detune_);
	float detune = input_detune * powf(interval, static_cast<float>(i + 1)) * powf(2.0f, cents_jitter / 1200.0f);
	spawn(child, input_position, detune, powf(0.5f, static_cast<float>(i + 1)));
      }
    };
    stacked(fourths_, 4.0f / 3.0f);
    stacked(fifths_, 3.0f / 2.0f);
    stacked(octaves_, 2.0f);
  }

  if (array_voice) {
    array_voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);
    group->addChild(voice_id, move(array_voice));
  }
  return group;
}

void
NoteMultiplier::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  unisons_ = input.get<int>("unisons", 1);
  fourths_ = input.get<int>("fourths");
  fifths_ = input.get<int>("fifths");
  octaves_ = input.get<int>("octaves");
  detune_ = input.get<float>("detune");
  spread_ = input.get<float>("spread");
}

void
NoteMultiplier::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  output.set("unisons", unisons_);
  output.set("octaves", octaves_);
  output.set("fifths", fifths_);
  output.set("fourths", fourths_);
  output.set("detune", detune_);
  output.set("spread", spread_);
}
