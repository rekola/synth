#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "../state/PositionedVoice.h"
#include "../dsp/HashField.h"

namespace {
// Fixed compile-time seed, not drawn from any shared sequence - matches
// SoundFont.cpp's kPercussionJitterSeed/bus/GranularCloud.cpp's
// kDirectionScatterSeed precedent (see their own doc comments): all the
// per-note variation lives in the coordinate, this salt just keeps the
// "start phase" axis decorrelated from every other HashField-derived
// value a note might draw (NoteMultiplier's own detune jitter,
// TapeDegradation's seed, ...).
constexpr uint64_t kNotePhaseSalt = 0xA1D4B4C9E3129F5Bull;
}

// A pitched leaf voice - PositionedVoice's own spatial identity
// (position/sends/floor reflection/encodePosition()) plus frequency,
// detune, and a phase accumulator for anything that renders a periodic
// waveform (Oscillator/SoundFont/...). A leaf voice with no pitch concept
// at all (SampleTrack.cpp's SampleClipVoice) derives from PositionedVoice
// directly instead, never through here.
class InstrumentVoice : public PositionedVoice {
 public:
  // Start phase (as a fraction of a second into the waveform, matching
  // this class's own historical start_phase contract) is derived here,
  // once, from whatever NoteCoordinate this voice was constructed with -
  // not passed in pre-computed. Every leaf voice type goes through this
  // one constructor, so this is the single place "which note gets which
  // phase" is decided, regardless of which of them get one (a NoiseVoice
  // ignores the resulting sourceSamplePosition_ entirely - it never calls
  // stepForward()/getSourceSamplePosition() - but still goes through the
  // same derivation for uniformity, not as a special case).
  InstrumentVoice(const ChannelConfiguration & channel_config, const SphericalPosition & position, float detune, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {})
    : PositionedVoice(channel_config, position, sends, note_coord),
      sourceSamplePosition_(HashField(kNotePhaseSalt).unit(note_coord.toHashCoord(), paramId("note_phase"))
                             * getChannelConfiguration().getAudioOutSampleRate()),
      detune_(detune)
  { }

  void killNote() override {
    VoiceState::killNote();
    freq_ = 0.0f;
  }

  void stopNote() override { killNote(); }

  // Every non-SF2 leaf already cuts instantly on stopNote() (via
  // killNote() above) - no separate release phase to shorten, so
  // fastRelease() just aliases it. SoundFontVoice overrides this with a
  // real short release instead (see VoiceState::fastRelease()'s comment).
  void fastRelease() override { stopNote(); }

  void playNote(float frequency, float velocity, int note_value) override {
    VoiceState::playNote(frequency, velocity, note_value);

    if (freq_ == 0.0f) {
      setGainDB(-gainToDecibels(1.0f / velocity));
    }
    freq_ = frequency * detune_;
    note_value_ = note_value;
    velocity_ = velocity;
  }

  bool isActive() const override { return freq_ != 0.0f; }

protected:
  double getSourceSamplePosition() const { return sourceSamplePosition_; }

  inline void stepForward(int frames) {
    sourceSamplePosition_ += freq_ * frames;
  }

  float getFrequency() const { return freq_; }
  float getDetune() const { return detune_; }

  double sourceSamplePosition_;

private:
  float freq_ = 0.0f;
  float detune_;
};

#endif
