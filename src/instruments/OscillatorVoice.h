#ifndef _OSCILLATORVOICE_H_
#define _OSCILLATORVOICE_H_

#include "InstrumentVoice.h"
#include "OscillatorKernel.h"
#include "OscillatorArray.h"
#include "PitchDrift.h"
#include "WaveformType.h"
#include "../ambisonic/AmbisonicStackEncoder.h"
#include "../ambisonic/SphericalPosition.h"
#include "../dsp/HashField.h"
#include "../model/NoteCoordinate.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr uint64_t kCloudScatterSalt = 0x7C1E9A4D2B6F3085ull;
}

// An oscillator voice: an array of buckets, each one direction with members
// dealt into it round-robin. A bucket's members are summed and encoded once,
// and every bucket is encoded in one pass. The buckets are laid out as
// concentric rings around the track's position (see cloudPoint()), turned and
// jittered per note so no two notes share one geometry, so a
// spread widens the image; with no spread there is one bucket at the
// position. The floor reflection and the Aux sends run once on the summed dry
// signal at the centre. One member is just the array's special case.
class OscillatorVoice : public InstrumentVoice {
public:
  // `detune` is the frequency ratio applied to every member (the played
  // note's own detune and harmonic).
  OscillatorVoice(const ChannelConfiguration & config, const SphericalPosition & position, float detune, WaveformType type, float level, float pulse_width, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {}, const OscillatorArray & array = {})
    : InstrumentVoice(config, position, 1.0f, sends, note_coord), type_(type), pulse_width_(pulse_width) {
    const int n = std::clamp(array.voices, 1, OscillatorArray::kMaxVoices);
    // atan2 rather than atan handles distance <= 0 (an untouched/diffuse
    // position, where the azimuth is ignored anyway) without dividing by 0.
    const float radius_deg = n > 1 ? atan2f(array.spread * position.extent, position.distance) * 180.0f / static_cast<float>(M_PI) : 0.0f;

    const int count = bucketCountFor(config.getAmbisonicOrder(), radius_deg, n, position.distance > 0.0f);
    buckets_.resize(static_cast<size_t>(count));
    const std::vector<int> rings = ringCounts(count);
    const int64_t scatter_coord = note_coord.toHashCoord();
    for (int b = 0; b < count; b++) {
      Bucket & bucket = buckets_[static_cast<size_t>(b)];
      bucket.direction = cloudPoint(position, radius_deg, b, rings, &scatter_coord);
      bucket.gains = computeAmbisonicGains(bucket.direction);
    }

    for (int k = 0; k < n; k++) {
      // Where the member sits across the detune range: -1 .. +1, or 0 for one.
      const float place = n > 1 ? 2.0f * static_cast<float>(k) / static_cast<float>(n - 1) - 1.0f : 0.0f;

      Member member;
      member.level = level * powf(array.falloff, static_cast<float>(k));
      member.ratio = detune * powf(array.ratio, static_cast<float>(k)) * powf(2.0f, place * array.detune_cents / 2400.0f);
      // The same derivation as InstrumentVoice's own start phase, so a
      // lone member starts where this voice always has; array members
      // are decorrelated by their index.
      NoteCoordinate coord = n > 1 ? note_coord.withInstance(k) : note_coord;
      member.phase = static_cast<double>(HashField(kNotePhaseSalt).unit(coord.toHashCoord(), paramId("note_phase")));
      if (n > 1 && array.detune_cents > 0.0f && array.drift_period > 0.0f) {
        const double period = std::max(array.drift_period, kMinDriftPeriod) * static_cast<double>(config.getAudioOutSampleRate());
        member.drift = PitchDrift(coord.toHashCoord(), 0.5f * array.detune_cents, period);
      }

      // Dealt round-robin, so the buckets stay evenly filled and members
      // neighbouring in pitch land in different places.
      buckets_[static_cast<size_t>(k % count)].members.push_back(member);
    }
  }

  // How many buckets an array of `members` needs: as many resolvable cells
  // (diameter `width` - the bus can't tell closer directions apart) as fit in
  // the cloud's area, an ellipse of azimuth radius `radius_deg` and elevation
  // radius `radius_deg / kExtentShapeRatio`; at least three (a triangle, so a
  // spread is two-dimensional) once the cloud is wider than one cell, but
  // never more than the members, and one when direction is meaningless (a
  // mono bus, a source with no distance, or no spread).
  static int bucketCountFor(int ambisonic_order, float radius_deg, int members, bool directional) {
    if (members <= 1 || ambisonic_order <= 0 || !directional || radius_deg <= 0.0f) return 1;
    const float width_deg = ambisonic_order >= 3 ? 12.0f : ambisonic_order == 2 ? 16.0f : 25.0f;
    const float cells = radius_deg * radius_deg / kExtentShapeRatio / ((width_deg / 2.0f) * (width_deg / 2.0f));
    if (cells < 1.0f) return 1;
    return std::clamp(static_cast<int>(std::lround(cells)), std::min(3, members), members);
  }

  // `buckets` points as concentric rings: ring j of J sits at radius j/J of
  // the cloud and holds a share of the points proportional to j (even
  // density). J rings hold about 3J(J+1) points at even spacing, so
  // J = ceil((sqrt(1 + 4B/3) - 1) / 2); the counts come from cumulative
  // rounding, C_j = round(B j(j+1) / (J(J+1))), n_j = C_j - C_(j-1), which
  // always sums to B. One bucket is the centre, with no ring.
  static std::vector<int> ringCounts(int buckets) {
    if (buckets <= 1) return {};
    const int rings = static_cast<int>(std::ceil((std::sqrt(1.0 + 4.0 * buckets / 3.0) - 1.0) / 2.0));
    std::vector<int> counts;
    int before = 0;
    for (int j = 1; j <= rings; j++) {
      const int upto = j == rings ? buckets : static_cast<int>(std::lround(static_cast<double>(buckets) * j * (j + 1) / (static_cast<double>(rings) * (rings + 1))));
      counts.push_back(upto - before);
      before = upto;
    }
    return counts;
  }

  // Bucket b on the given rings (ringCounts() of the bucket count), around `center`, on an ellipse of
  // azimuth radius `radius_deg` and elevation radius `radius_deg /
  // kExtentShapeRatio`. Points run clockwise from the top (from the left for
  // a ring of two), and every second ring is staggered by half a step so
  // its points fall between its neighbour's. With `scatter_coord` each ring is
  // also turned by a hashed angle and each point moved by up to a quarter of
  // its spacing, kept inside the ellipse: the same note always gets the same
  // layout, different notes get different ones.
  static SphericalPosition cloudPoint(const SphericalPosition & center, float radius_deg, int b, const std::vector<int> & counts, const int64_t * scatter_coord = nullptr) {
    SphericalPosition p = center;
    if (counts.empty()) return p;

    int index = b;
    size_t ring = 0;
    while (index >= counts[ring]) index -= counts[ring++];

    const float step_deg = 360.0f / static_cast<float>(counts[ring]);
    const float start_deg = (counts[ring] == 2 ? 180.0f : 90.0f) + (ring % 2 == 1 ? step_deg / 2.0f : 0.0f);
    const float radius = radius_deg * static_cast<float>(ring + 1) / static_cast<float>(counts.size());

    float turn_deg = 0.0f, jitter_x = 0.0f, jitter_y = 0.0f;
    if (scatter_coord) {
      const HashField hash(kCloudScatterSalt);
      const uint32_t id = static_cast<uint32_t>(b);
      turn_deg = hash.unit(*scatter_coord, paramId("cloud_turn") + static_cast<uint32_t>(ring)) * step_deg;
      const float arc = counts[ring] > 1 ? 2.0f * static_cast<float>(M_PI) * radius / static_cast<float>(counts[ring]) : radius_deg;
      const float amplitude = 0.25f * std::min(arc, radius_deg / static_cast<float>(counts.size()));
      jitter_x = hash.bipolar(*scatter_coord, paramId("cloud_jitter_x") + id, amplitude);
      jitter_y = hash.bipolar(*scatter_coord, paramId("cloud_jitter_y") + id, amplitude);
    }

    const float angle = (start_deg + turn_deg - step_deg * static_cast<float>(index)) * static_cast<float>(M_PI) / 180.0f;
    float x = radius * cosf(angle) + jitter_x, y = radius * sinf(angle) + jitter_y;
    const float length = hypotf(x, y);
    if (length > radius_deg) {
      x *= radius_deg / length;
      y *= radius_deg / length;
    }
    p.azimuth += x;
    p.elevation += y / kExtentShapeRatio;
    return p;
  }

  size_t memberCount() const {
    size_t n = 0;
    for (const auto & bucket : buckets_) n += bucket.members.size();
    return n;
  }
  size_t bucketCount() const { return buckets_.size(); }
  size_t bucketSize(size_t b) const { return buckets_[b].members.size(); }
  const SphericalPosition & bucketDirection(size_t b) const { return buckets_[b].direction; }
  size_t bucketOf(size_t member) const { return member % buckets_.size(); }

  void playNote(float frequency, float velocity, int note_value) override {
    // The note's velocity is baked in once, at the first play.
    if (getFrequency() == 0.0f) {
      for (auto & bucket : buckets_) {
	for (auto & member : bucket.members) member.level *= velocity;
      }
    }
    InstrumentVoice::playNote(frequency, velocity, note_value);
  }

  void adjustAzimuth(float delta) override {
    InstrumentVoice::adjustAzimuth(delta);
    for (auto & bucket : buckets_) {
      bucket.direction.azimuth += delta;
      bucket.gains = computeAmbisonicGains(bucket.direction);
    }
  }

  AudioBuffer render(int frames) override {
    const double rate = static_cast<double>(getFrequency()) / getChannelConfiguration().getAudioOutSampleRate();
    const size_t padded = oscillator_kernel::paddedFrames(frames);

    const bool has_main = getSends().main > 0.0f;
    const float main_gain = getSends().main * getDistanceGain();
    AudioBuffer data = makeSendBuffer(frames);

    // Each bucket's members are summed into its own row, then every row is
    // encoded in one pass.
    const size_t rows = buckets_.size();
    bucket_sums_.assign(rows * padded, 0.0f);
    for (size_t b = 0; b < rows; b++) {
      float * row = bucket_sums_.data() + b * padded;
      for (auto & member : buckets_[b].members) {
	const double step = member.ratio * rate;
	oscillator_kernel::mix(type_, pulse_width_, member.phase, step, member.level, frames, row, &member.drift, age_);
	member.phase += step * frames;
	if (member.drift.active()) member.phase += step * (member.drift.integral(age_ + static_cast<uint64_t>(frames)) - member.drift.integral(age_));
	member.phase -= std::floor(member.phase);
      }
    }

    age_ += static_cast<uint64_t>(frames);

    if (has_main) {
      targets_.resize(rows);
      for (size_t b = 0; b < rows; b++) {
	targets_[b] = buckets_[b].gains;
	for (auto & gain : targets_[b]) gain *= main_gain;
      }
      encoder_.encodeBlock(data, bucket_sums_.data(), padded, targets_, frames);
    }

    // The array's whole dry signal, for the floor reflection and the sends
    // (the one row when there's a single bucket), summed only if one needs it.
    const float * dry = bucket_sums_.data();
    const bool reflect = has_main && floorReflectionActive();
    const bool sends = getSends().a > 0.0f || getSends().b > 0.0f;
    if (rows > 1 && (reflect || sends)) {
      sum_.assign(padded, 0.0f);
      for (size_t b = 0; b < rows; b++) {
	const float * row = bucket_sums_.data() + b * padded;
	for (size_t k = 0; k < padded; k++) sum_[k] += row[k];
      }
      dry = sum_.data();
    }

    if (reflect) addFloorReflection(data, dry, frames, main_gain);
    addAuxSends(data, dry, frames);
    return data;
  }

private:
  struct Member {
    double phase = 0.0;  // cycles
    float ratio = 1.0f;  // frequency ratio to the note
    float level = 1.0f;
    PitchDrift drift; // inactive unless the array drifts
  };

  struct Bucket {
    SphericalPosition direction;
    AmbisonicGains gains{};
    std::vector<Member> members;
  };

  WaveformType type_;
  float pulse_width_;
  static constexpr float kMinDriftPeriod = 0.05f; // seconds

  std::vector<Bucket> buckets_;
  uint64_t age_ = 0; // samples rendered since note-on, the drift's clock
  AmbisonicStackEncoder encoder_;
  std::vector<AmbisonicGains> targets_;
  std::vector<float> bucket_sums_, sum_;
};

#endif
