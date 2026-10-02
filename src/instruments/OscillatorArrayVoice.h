#ifndef _OSCILLATORARRAYVOICE_H_
#define _OSCILLATORARRAYVOICE_H_

#include "InstrumentVoice.h"
#include "OscillatorArray.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"

#include <vector>

// One voice standing in for a whole stack of simultaneous Oscillator copies
// (NoteMultiplier's unisons/fourths/fifths/octaves) - an OscillatorArray
// rendered in one pass and mixed into this single voice's buffer. Each copy
// keeps its own direction, so a unison spread still widens the image, but
// the floor reflection and the Aux sends are done once on the summed dry
// signal at the centre position rather than once per copy.
class OscillatorArrayVoice : public InstrumentVoice {
 public:
  struct CopySpec {
    WaveformType type;
    float level;              // the oscillator's own level
    float pulse_width;
    float ratio;              // frequency ratio to the note's own frequency
    float velocity_scale;     // relative to the note's velocity
    SphericalPosition position;
    NoteCoordinate coord;     // this copy's own identity, sets its start phase
  };

  // `position` is the stack's centre, used for the floor reflection.
  OscillatorArrayVoice(const ChannelConfiguration & config, const SphericalPosition & position, const SendLevels & sends, const NoteCoordinate & note_coord)
    : InstrumentVoice(config, position, 1.0f, sends, note_coord) { }

  void addCopy(const CopySpec & spec) {
    OscillatorArray::Copy copy;
    copy.type = spec.type;
    copy.level = spec.level * spec.velocity_scale;
    copy.pulse_width = spec.pulse_width;
    copy.ratio = static_cast<double>(spec.ratio);
    // Same derivation as InstrumentVoice's own start phase, so a copy
    // starts exactly where a separate voice with this coordinate would.
    copy.phase = static_cast<double>(HashField(kNotePhaseSalt).unit(spec.coord.toHashCoord(), paramId("note_phase")));
    array_.add(copy);
    positions_.push_back(spec.position);
    encoders_.emplace_back();
  }

  size_t copyCount() const { return array_.size(); }

  void playNote(float frequency, float velocity, int note_value) override {
    // The note's velocity is baked in once, at the first play, like
    // InstrumentVoice's own gain.
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
