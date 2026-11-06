#ifndef _INSTRUMENTVOICE_H_
#define _INSTRUMENTVOICE_H_

#include "../state/PositionedVoice.h"
#include "../dsp/HashField.h"

#include <cmath>

namespace {
// Fixed compile-time seed, not drawn from any shared sequence - matches
// SoundFont.cpp's kPercussionJitterSeed/bus/GranularCloud.cpp's
// kDirectionScatterSeed precedent (see their own doc comments): all the
// per-note variation lives in the coordinate, this salt just keeps the
// "start phase" axis decorrelated from every other HashField-derived
// value a note might draw (TapeDegradation's seed, ...).
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

  // 0Uxx/0Dxx slide: the pitch offset (cents, relative to where this voice
  // started) at the chunk's first and last sample. The first call sets the
  // origin, so a note started mid-slide begins at its own pitch.
  void setPitchRamp(double start_cents, double end_cents) override {
    VoiceState::setPitchRamp(start_cents, end_cents);
    if (!slide_origin_set_) {
      slide_origin_ = start_cents;
      slide_origin_set_ = true;
    }
    slide_start_cents_ = start_cents - slide_origin_;
    slide_end_cents_ = end_cents - slide_origin_;
    updateRatios();
  }

  // 0Gxx glide: the glide offset is separate from the slide offset and the
  // two add. It moves a chunk at a time, linearly across the chunk.
  void setPitchGlideTarget(double cents) override {
    VoiceState::setPitchGlideTarget(cents);
    glide_target_ = cents;
  }
  void advancePitchGlide(double cents_per_frame, int frames) override {
    VoiceState::advancePitchGlide(cents_per_frame, frames);
    const double distance = glide_target_ - glide_now_;
    const double step = cents_per_frame * frames;
    const double start = glide_now_;
    if (step > 0.0 && distance != 0.0) glide_now_ = std::fabs(distance) <= step ? glide_target_ : glide_now_ + std::copysign(step, distance);
    if (start == glide_start_ && glide_now_ == glide_end_) return;
    glide_start_ = start;
    glide_end_ = glide_now_;
    updateRatios();
  }
  void jumpPitchGlide() override {
    VoiceState::jumpPitchGlide();
    glide_now_ = glide_target_;
  }

protected:
  double getSourceSamplePosition() const { return sourceSamplePosition_; }

  inline void stepForward(int frames) {
    // The increment of sample k is f0 + (f1 - f0) * k / frames (it reaches f1
    // at the next chunk's first sample), so the chunk covers exactly the sum
    // of that over k < frames - the position the next chunk must start from.
    const double f0 = static_cast<double>(getFrequency()), f1 = static_cast<double>(getFrequencyEnd());
    sourceSamplePosition_ += f0 * frames + (f1 - f0) * 0.5 * (frames - 1);
  }

  // The frequency at the chunk's first sample, and at its last.
  float getFrequency() const { return freq_ * slide_ratio_start_; }
  float getFrequencyEnd() const { return freq_ * slide_ratio_end_; }
  // The pitch ratio (slide and glide) at the chunk's first and last sample.
  float getSlideRatioStart() const { return slide_ratio_start_; }
  float getSlideRatioEnd() const { return slide_ratio_end_; }
  float getDetune() const { return detune_; }

  double sourceSamplePosition_;

private:
  float freq_ = 0.0f;
  float detune_;
  // The offsets are unbounded (the song's pitch may run as far as a song
  // takes it); only the ratio is held to what a voice can still compute,
  // 20 octaves either way.
  static float slideRatio(double cents) {
    constexpr double kLimit = 24000.0;
    if (!(cents > -kLimit)) cents = -kLimit;
    if (cents > kLimit) cents = kLimit;
    return std::exp2(static_cast<float>(cents) / 1200.0f);
  }
  void updateRatios() {
    slide_ratio_start_ = slideRatio(slide_start_cents_ + glide_start_);
    slide_ratio_end_ = slideRatio(slide_end_cents_ + glide_end_);
  }

  double slide_origin_ = 0.0;
  bool slide_origin_set_ = false;
  double slide_start_cents_ = 0.0, slide_end_cents_ = 0.0;
  double glide_target_ = 0.0, glide_now_ = 0.0, glide_start_ = 0.0, glide_end_ = 0.0;
  float slide_ratio_start_ = 1.0f, slide_ratio_end_ = 1.0f;
};

#endif
