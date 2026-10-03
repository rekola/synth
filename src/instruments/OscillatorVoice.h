#ifndef _OSCILLATORVOICE_H_
#define _OSCILLATORVOICE_H_

#include "InstrumentVoice.h"
#include "OscillatorArray.h"
#include "OscillatorStack.h"
#include "WaveformType.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"

#include <algorithm>
#include <cmath>
#include <vector>

// An oscillator voice: a stack of OscillatorArray members rendered in one
// pass and mixed into this single voice's buffer. Each member keeps its own
// direction, so a spread still widens the image. Encoding is linear, so
// members sharing a direction (every member, when there's no spread) are
// summed first and encoded once; the floor reflection and the Aux sends run
// once on the summed dry signal at the centre position. One member is just
// the stack's special case.
class OscillatorVoice : public InstrumentVoice {
public:
  // `detune` is the frequency ratio applied to every member (the played
  // note's own detune and harmonic).
  OscillatorVoice(const ChannelConfiguration & config, const SphericalPosition & position, float detune, WaveformType type, float level, float pulse_width, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {}, const OscillatorStack & stack = {})
    : InstrumentVoice(config, position, 1.0f, sends, note_coord) {
    const int n = std::clamp(stack.voices, 1, OscillatorStack::kMaxVoices);

    // Where the members sit across the spread: -1 .. +1, or 0 for one.
    auto place = [&](int k) { return n > 1 ? 2.0f * static_cast<float>(k) / static_cast<float>(n - 1) - 1.0f : 0.0f; };

    // atan2 rather than atan handles distance <= 0 (an untouched/diffuse
    // position, where the azimuth is ignored anyway) without dividing by 0.
    const float half_width_deg = n > 1 ? atan2f(stack.spread * position.extent, position.distance) * 180.0f / static_cast<float>(M_PI) : 0.0f;

    for (int k = 0; k < n; k++) {
      OscillatorArray::Copy copy;
      copy.type = type;
      copy.level = level * powf(stack.falloff, static_cast<float>(k));
      copy.pulse_width = pulse_width;
      copy.ratio = static_cast<double>(detune * powf(stack.ratio, static_cast<float>(k)) * powf(2.0f, place(k) * stack.detune_cents / 2400.0f));
      // The same derivation as InstrumentVoice's own start phase, so a
      // lone member starts where this voice always has; stacked members
      // are decorrelated by their index.
      NoteCoordinate coord = n > 1 ? note_coord.withInstance(k) : note_coord;
      copy.phase = static_cast<double>(HashField(kNotePhaseSalt).unit(coord.toHashCoord(), paramId("note_phase")));
      array_.add(copy);

      SphericalPosition member = position;
      member.azimuth += place(k) * half_width_deg;
      // Weighted by the shared shape ratio so a wide stack doesn't collapse
      // onto one flat horizontal line.
      member.elevation += place(k) * half_width_deg / kExtentShapeRatio;

      // Join the group of members already at exactly this direction.
      size_t g = 0;
      while (g < group_positions_.size() && !(group_positions_[g].azimuth == member.azimuth && group_positions_[g].elevation == member.elevation)) g++;
      if (g == group_positions_.size()) {
        group_positions_.push_back(member);
        group_members_.emplace_back();
        group_encoders_.emplace_back();
      }
      group_members_[g].push_back(static_cast<size_t>(k));
    }
  }

  size_t memberCount() const { return array_.size(); }

  // How many distinct directions the members are encoded from.
  size_t directionCount() const { return group_positions_.size(); }

  void playNote(float frequency, float velocity, int note_value) override {
    // The note's velocity is baked in once, at the first play.
    if (getFrequency() == 0.0f) array_.scaleLevels(velocity);
    InstrumentVoice::playNote(frequency, velocity, note_value);
  }

  void adjustAzimuth(float delta) override {
    InstrumentVoice::adjustAzimuth(delta);
    for (auto & p : group_positions_) p.azimuth += delta;
  }

  AudioBuffer render(int frames) override {
    const double rate = static_cast<double>(getFrequency()) / getChannelConfiguration().getAudioOutSampleRate();
    const size_t padded = OscillatorArray::paddedFrames(frames);
    if (scratch_.size() < padded) scratch_.resize(padded);

    if (array_.size() == 1) {
      array_.renderCopy(0, rate, frames, scratch_.data());
      array_.advance(rate, frames);
      return encodePosition(scratch_.data(), frames);
    }

    const bool has_main = getSends().main > 0.0f;
    const float main_gain = getSends().main * getDistanceGain();
    AudioBuffer data = makeSendBuffer(frames);

    // One direction: the members' sum is both the stack's dry signal and
    // the one thing to encode.
    sum_.assign(padded, 0.0f);
    if (group_positions_.size() == 1) {
      for (size_t i = 0; i < array_.size(); i++) array_.mixCopy(i, rate, frames, sum_.data());
      if (has_main) encodeGroup(0, sum_.data(), frames, main_gain, data);
    } else {
      for (size_t g = 0; g < group_positions_.size(); g++) {
	scratch_.assign(padded, 0.0f);
	for (size_t i : group_members_[g]) array_.mixCopy(i, rate, frames, scratch_.data());
	if (has_main) encodeGroup(g, scratch_.data(), frames, main_gain, data);
	for (size_t k = 0; k < static_cast<size_t>(frames); k++) sum_[k] += scratch_[k];
      }
    }
    array_.advance(rate, frames);

    if (has_main) addFloorReflection(data, sum_.data(), frames, main_gain);
    addAuxSends(data, sum_.data(), frames);
    return data;
  }

private:
  void encodeGroup(size_t g, const float * dry, int frames, float main_gain, AudioBuffer & data) {
    auto gains = computeAmbisonicGains(group_positions_[g]);
    for (auto & gain : gains) gain *= main_gain;
    group_encoders_[g].encodeBlock(data, dry, frames, gains);
  }

  OscillatorArray array_;
  // Per distinct direction: where it is, which members sit there, and its
  // own gain-interpolating encoder.
  std::vector<SphericalPosition> group_positions_;
  std::vector<std::vector<size_t>> group_members_;
  std::vector<AmbisonicVoiceEncoder> group_encoders_;
  std::vector<float> scratch_, sum_;
};

#endif
