#ifndef _OSCILLATORVOICE_H_
#define _OSCILLATORVOICE_H_

#include "InstrumentVoice.h"
#include "OscillatorArray.h"
#include "OscillatorStack.h"
#include "WaveformType.h"
#include "../ambisonic/AmbisonicStackEncoder.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"

#include <algorithm>
#include <cmath>
#include <vector>

// An oscillator voice: a stack of OscillatorArray members rendered in one
// pass and mixed into this single voice's buffer. Each member keeps its own
// direction, so a spread still widens the image. Encoding is linear, so
// members whose directions are closer than the ambisonic order can resolve
// share a bucket: their signals are summed first and encoded once, with the
// mean of their gain vectors (a few buckets stand in for many members; no
// spread means one bucket). The floor reflection and the Aux sends run once
// on the summed dry signal at the centre position. One member is just the
// stack's special case.
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
      member_positions_.push_back(member);
    }

    // Members run in order along the line, so a bucket is a contiguous run.
    const int buckets = bucketCountFor(config.getAmbisonicOrder(), half_width_deg, n, position.distance > 0.0f);
    bucket_members_.resize(static_cast<size_t>(buckets));
    for (int k = 0; k < n; k++) bucket_members_[static_cast<size_t>(k * buckets / n)].push_back(static_cast<size_t>(k));
    updateBucketGains();
  }

  // How many buckets `members` spread over a line of +-`half_width_deg` fall
  // into: one per resolvable width of the ambisonic order (the bus can't tell
  // closer directions apart), at most kMaxBuckets, and one when the direction
  // is meaningless (mono bus, or a source with no distance).
  static constexpr int kMaxBuckets = 8;
  static int bucketCountFor(int ambisonic_order, float half_width_deg, int members, bool directional) {
    if (members <= 1 || ambisonic_order <= 0 || !directional || half_width_deg <= 0.0f) return 1;
    const float width_deg = ambisonic_order >= 3 ? 12.0f : ambisonic_order == 2 ? 16.0f : 25.0f;
    const int wanted = static_cast<int>(std::ceil(2.0f * half_width_deg / width_deg));
    return std::clamp(wanted, 1, std::min(kMaxBuckets, members));
  }

  size_t memberCount() const { return array_.size(); }

  // How many buckets the members are encoded through.
  size_t directionCount() const { return bucket_members_.size(); }

  void playNote(float frequency, float velocity, int note_value) override {
    // The note's velocity is baked in once, at the first play.
    if (getFrequency() == 0.0f) array_.scaleLevels(velocity);
    InstrumentVoice::playNote(frequency, velocity, note_value);
  }

  void adjustAzimuth(float delta) override {
    InstrumentVoice::adjustAzimuth(delta);
    for (auto & p : member_positions_) p.azimuth += delta;
    updateBucketGains();
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

    // Each bucket's members are summed into its own row, then every row is
    // encoded in one pass.
    const size_t groups = bucket_members_.size();
    group_sums_.assign(groups * padded, 0.0f);
    for (size_t g = 0; g < groups; g++) {
      for (size_t i : bucket_members_[g]) array_.mixCopy(i, rate, frames, group_sums_.data() + g * padded);
    }
    array_.advance(rate, frames);

    if (has_main) {
      targets_.resize(groups);
      for (size_t g = 0; g < groups; g++) {
	targets_[g] = bucket_gains_[g];
	for (auto & gain : targets_[g]) gain *= main_gain;
      }
      encoder_.encodeBlock(data, group_sums_.data(), padded, targets_, frames);
    }

    // The stack's whole dry signal, for the floor reflection and the sends:
    // the one row when there's a single bucket.
    const float * dry = group_sums_.data();
    if (groups > 1) {
      sum_.assign(padded, 0.0f);
      for (size_t g = 0; g < groups; g++) {
	const float * row = group_sums_.data() + g * padded;
	for (size_t k = 0; k < padded; k++) sum_[k] += row[k];
      }
      dry = sum_.data();
    }

    if (has_main) addFloorReflection(data, dry, frames, main_gain);
    addAuxSends(data, dry, frames);
    return data;
  }

private:
  // Each bucket's gains: the mean of its members' own gain vectors (the
  // first member's exactly, when they're all at one spot). Recomputed only
  // when the members move.
  void updateBucketGains() {
    bucket_gains_.assign(bucket_members_.size(), AmbisonicGains{});
    for (size_t b = 0; b < bucket_members_.size(); b++) {
      const auto & members = bucket_members_[b];
      const SphericalPosition & first = member_positions_[members[0]];
      bool same_spot = true;
      for (size_t k : members) same_spot = same_spot && member_positions_[k].azimuth == first.azimuth && member_positions_[k].elevation == first.elevation;

      if (same_spot) {
	bucket_gains_[b] = computeAmbisonicGains(first);
	continue;
      }
      for (size_t k : members) {
	auto g = computeAmbisonicGains(member_positions_[k]);
	for (size_t c = 0; c < g.size(); c++) bucket_gains_[b][c] += g[c];
      }
      for (auto & gain : bucket_gains_[b]) gain /= static_cast<float>(members.size());
    }
  }

  OscillatorArray array_;
  std::vector<SphericalPosition> member_positions_;
  std::vector<std::vector<size_t>> bucket_members_;
  std::vector<AmbisonicGains> bucket_gains_;
  AmbisonicStackEncoder encoder_;
  std::vector<AmbisonicGains> targets_;
  std::vector<float> scratch_, group_sums_, sum_;
};

#endif
