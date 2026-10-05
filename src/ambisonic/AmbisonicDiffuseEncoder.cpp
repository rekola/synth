#include "AmbisonicDiffuseEncoder.h"

#include "../dsp/Vec8.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {

bool isPrime(int n) {
  if (n < 2) return false;
  for (int d = 2; d * d <= n; d++) {
    if (n % d == 0) return false;
  }
  return true;
}

// Every prime number of samples within the 5-20ms window at `sampleRate`
// - primes are pairwise coprime by construction, which is what keeps the
// allpass chains' delay lengths from lining up into a shared periodicity
// (see the class's own doc comment). Computed once per distinct
// sampleRate (function-local static keyed by nothing but sampleRate
// itself is unnecessary here - every AmbisonicDiffuseEncoder instance in
// this process shares the same sampleRate in practice, so a plain
// per-call build, cached by the caller, is simplest); this function is
// only ever called from the constructor, never per-block.
vector<int> delayLengthTable(int sampleRate) {
  int lo = static_cast<int>(0.005f * static_cast<float>(sampleRate));
  int hi = static_cast<int>(0.020f * static_cast<float>(sampleRate));
  vector<int> primes;
  for (int n = lo; n <= hi; n++) {
    if (isPrime(n)) primes.push_back(n);
  }
  return primes;
}

}

void
AmbisonicDiffuseEncoder::AllpassStage::process(const float * x, float * y, int frames) {
  const dsp::v8f gain = dsp::splat(kAllpassGain);
  float * buf = buffer.data();

  while (frames > 0) {
    const int run = min(frames, length - pos);
    float * slot = buf + pos;

    int i = 0;
    for (; i + dsp::kLanes <= run; i += dsp::kLanes) {
      const dsp::v8f delayed = dsp::loadu(slot + i);
      const dsp::v8f v = dsp::loadu(x + i) - gain * delayed;
      dsp::storeu(slot + i, v);
      dsp::storeu(y + i, delayed + gain * v);
    }
    for (; i < run; i++) {
      const float delayed = slot[i];
      const float v = x[i] - kAllpassGain * delayed;
      slot[i] = v;
      y[i] = delayed + kAllpassGain * v;
    }

    pos += run;
    if (pos >= length) pos = 0;
    x += run;
    y += run;
    frames -= run;
  }
}

void
AmbisonicDiffuseEncoder::Chain::process(const float * in, float * out, int frames) {
  stages[0].process(in, out, frames);
  for (size_t s = 1; s < stages.size(); s++) stages[s].process(out, out, frames);
}

AmbisonicDiffuseEncoder::AmbisonicDiffuseEncoder(int sampleRate, uint32_t instanceSalt) {
  auto table = delayLengthTable(sampleRate);
  // See the constructor's own doc comment: a cyclic shift into the shared
  // table, not a disjoint per-instance slice - the table isn't always
  // large enough (e.g. ~105 primes at 44.1kHz) to hand every one of a
  // single instance's own 64 (channel, stage) slots a value with zero
  // reuse across instances too, but a large shift keeps two instances'
  // full 64-length sequences from ever coinciding.
  constexpr uint32_t kShiftStride = 37; // coprime with most plausible table sizes
  uint32_t base = instanceSalt * kShiftStride;

  for (int c = 0; c < kAmbisonicChannelCount; c++) {
    for (int s = 0; s < kStagesPerChannel; s++) {
      size_t idx = (base + static_cast<uint32_t>(c * kStagesPerChannel + s)) % table.size();
      int length = table[idx];
      auto & stage = chains_[static_cast<size_t>(c)].stages[static_cast<size_t>(s)];
      stage.length = length;
      stage.buffer.assign(static_cast<size_t>(length), 0.0f);
      stage.pos = 0;
    }
  }
}

void
AmbisonicDiffuseEncoder::encode(AudioBuffer & out, const float * mono, int frames, float diffusion, float gain) {
  int regular = out.regularChannelCount();
  int n = min(regular, kAmbisonicChannelCount);
  if (n <= 0) return;

  if (static_cast<int>(scratch_.size()) != frames) scratch_.resize(static_cast<size_t>(frames));

  for (int c = 0; c < n; c++) {
    int degree = acnDegree(c);

    // Per-degree taper - see the class's own doc comment for why this is
    // a segment ramp, not a flat scale, and why degree 0 is exempt.
    float taper = 1.0f;
    if (degree > 0) {
      int segment = 3 - degree; // degree 3 -> segment 0 (ramps first), degree 1 -> segment 2 (ramps last)
      float lo = static_cast<float>(segment) / 3.0f;
      float hi = static_cast<float>(segment + 1) / 3.0f;
      taper = (diffusion - lo) / (hi - lo);
      if (taper < 0.0f) taper = 0.0f;
      if (taper > 1.0f) taper = 1.0f;
    }

    float weight = sqrtf(2.0f * static_cast<float>(degree) + 1.0f) * taper * gain;

    auto & chain = chains_[static_cast<size_t>(c)];
    chain.process(mono, scratch_.data(), frames);
    dsp::addScaled(out.getChannelData(c), scratch_.data(), weight, frames);
  }
}
