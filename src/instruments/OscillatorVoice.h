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
// direction, so a spread still widens the image, but the floor reflection
// and the Aux sends run once on the summed dry signal at the centre
// position. One member is just the stack's special case.
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
      positions_.push_back(member);
      encoders_.emplace_back();
    }
  }

  size_t memberCount() const { return array_.size(); }

  void playNote(float frequency, float velocity, int note_value) override {
    // The note's velocity is baked in once, at the first play.
    if (getFrequency() == 0.0f) array_.scaleLevels(velocity);
    InstrumentVoice::playNote(frequency, velocity, note_value);
  }

  void adjustAzimuth(float delta) override {
    InstrumentVoice::adjustAzimuth(delta);
    for (auto & p : positions_) p.azimuth += delta;
  }

  AudioBuffer render(int frames) override {
    const double rate = static_cast<double>(getFrequency()) / getChannelConfiguration().getAudioOutSampleRate();
    const size_t n = static_cast<size_t>(frames);

    if (scratch_.size() < OscillatorArray::paddedFrames(frames)) scratch_.resize(OscillatorArray::paddedFrames(frames));

    if (array_.size() == 1) {
      array_.renderCopy(0, rate, frames, scratch_.data());
      array_.advance(rate, frames);
      return encodePosition(scratch_.data(), frames);
    }

    sum_.assign(n, 0.0f);

    const bool has_main = getSends().main > 0.0f;
    const float main_gain = getSends().main * getDistanceGain();
    AudioBuffer data = makeSendBuffer(frames);

    for (size_t i = 0; i < array_.size(); i++) {
      array_.renderCopy(i, rate, frames, scratch_.data());
      for (size_t k = 0; k < n; k++) sum_[k] += scratch_[k];

      if (has_main) {
	auto gains = computeAmbisonicGains(positions_[i]);
	for (auto & g : gains) g *= main_gain;
	encoders_[i].encodeBlock(data, scratch_.data(), frames, gains);
      }
    }
    array_.advance(rate, frames);

    if (has_main) addFloorReflection(data, sum_.data(), frames, main_gain);
    addAuxSends(data, sum_.data(), frames);
    return data;
  }

private:
  OscillatorArray array_;
  std::vector<SphericalPosition> positions_;
  std::vector<AmbisonicVoiceEncoder> encoders_;
  std::vector<float> scratch_, sum_;
};

#endif
