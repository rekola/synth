#include "SinusoidBank.h"
#include "../dsp/Vec8.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {
constexpr float kCullThresholdDb = -90.0f;
constexpr float kPi = static_cast<float>(M_PI);

inline float cullThresholdRatio() {
  static const float ratio = powf(10.0f, kCullThresholdDb / 20.0f);
  return ratio;
}
} // namespace

SinusoidBank::SinusoidBank(const vector<PartialSpec> & specs, float sample_rate) {
  const float nyquist = sample_rate * 0.5f;

  int group_count = 0;
  for (const auto & spec : specs) group_count = max(group_count, spec.group + 1);
  groups_.resize(static_cast<size_t>(group_count));

  // Each group's partials go in construction order, padded to whole lane
  // groups, so the slots past a group's active count stay zero and add nothing.
  vector<vector<const PartialSpec *>> members(static_cast<size_t>(group_count));
  for (const auto & spec : specs) {
    if (spec.group < 0 || spec.frequency_hz >= nyquist) continue;
    members[static_cast<size_t>(spec.group)].push_back(&spec);
  }

  size_t total = 0;
  for (size_t g = 0; g < groups_.size(); g++) {
    groups_[g].begin = total;
    groups_[g].active = static_cast<int>(members[g].size());
    groups_[g].padded = (members[g].size() + dsp::kLanes - 1) / dsp::kLanes * dsp::kLanes;
    total += groups_[g].padded;
  }
  for (auto * v : {&coeff_, &y1_, &y2_, &amp_, &decay_mult_, &min_amp_}) v->assign(total, 0.0f);

  for (size_t g = 0; g < groups_.size(); g++) {
    for (size_t i = 0; i < members[g].size(); i++) {
      const PartialSpec & spec = *members[g][i];
      const size_t at = groups_[g].begin + i;
      const float w = 2.0f * kPi * spec.frequency_hz / sample_rate;
      coeff_[at] = 2.0f * cosf(w);
      // Coupled-form init: y2 holds y[-1] = sin(phase - w), y1 holds
      // y[0] = sin(phase), the first sample render() outputs.
      y2_[at] = sinf(spec.phase - w);
      y1_[at] = sinf(spec.phase);
      amp_[at] = spec.amplitude;
      decay_mult_[at] = expf(-spec.alpha / sample_rate);
      min_amp_[at] = spec.amplitude * cullThresholdRatio();
      active_total_++;
    }
  }
}

void SinusoidBank::render(float * rows, size_t stride, int frames) {
  using dsp::kLanes;
  using dsp::v8f;

  // Partials are processed eight at a time with their state held in
  // registers across a chunk of frames; each chunk's per-frame vector sums
  // are folded into one value per frame at the end of the group.
  constexpr int kChunk = 64;

  for (size_t g = 0; g < groups_.size(); g++) {
    const Group & group = groups_[g];
    if (group.active == 0) continue;
    float * out = rows + g * stride;
    const size_t lane_groups = (static_cast<size_t>(group.active) + kLanes - 1) / kLanes;

    for (int base = 0; base < frames; base += kChunk) {
      const int n = frames - base < kChunk ? frames - base : kChunk;
      v8f sums[kChunk];
      for (int k = 0; k < n; k++) sums[k] = dsp::splat(0.0f);

      for (size_t l = 0; l < lane_groups; l++) {
        const size_t at = group.begin + l * kLanes;
        const v8f coeff = dsp::loadu(&coeff_[at]);
        const v8f decay = dsp::loadu(&decay_mult_[at]);
        v8f y1 = dsp::loadu(&y1_[at]);
        v8f y2 = dsp::loadu(&y2_[at]);
        v8f amp = dsp::loadu(&amp_[at]);

        for (int k = 0; k < n; k++) {
          sums[k] += y1 * amp;
          const v8f next = coeff * y1 - y2;
          y2 = y1;
          y1 = next;
          amp *= decay;
        }

        dsp::storeu(&y1_[at], y1);
        dsp::storeu(&y2_[at], y2);
        dsp::storeu(&amp_[at], amp);
      }

      for (int k = 0; k < n; k++) out[base + k] += dsp::hsum(sums[k]);
    }
  }

  cullDecayedPartials();
}

void
SinusoidBank::cullDecayedPartials() {
  for (auto & group : groups_) {
    const int before = group.active;
    size_t p = group.begin;
    while (p < group.begin + static_cast<size_t>(group.active)) {
      if (amp_[p] < min_amp_[p]) {
        const size_t last = group.begin + static_cast<size_t>(group.active) - 1;
        coeff_[p] = coeff_[last];
        y1_[p] = y1_[last];
        y2_[p] = y2_[last];
        amp_[p] = amp_[last];
        decay_mult_[p] = decay_mult_[last];
        min_amp_[p] = min_amp_[last];
        group.active--;
        // The swapped-in partial needs its own check, so p stays.
      } else {
        p++;
      }
    }

    for (int i = group.active; i < before; i++) {
      const size_t at = group.begin + static_cast<size_t>(i);
      for (auto * v : {&coeff_, &y1_, &y2_, &amp_, &decay_mult_, &min_amp_}) (*v)[at] = 0.0f;
    }
    active_total_ -= before - group.active;
  }
}

float
SinusoidBank::getPartialAmplitudeForTest(int index) const {
  if (index < 0) return 0.0f;
  for (const auto & group : groups_) {
    if (index < group.active) return amp_[group.begin + static_cast<size_t>(index)];
    index -= group.active;
  }
  return 0.0f;
}
