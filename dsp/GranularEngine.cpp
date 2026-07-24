#include "GranularEngine.h"

#include <cmath>

using namespace std;

namespace {

// Longest capture history this class ever allocates for, regardless of
// how far back a caller's own scanPosition+scanJitter asks to read - same
// "fixed ceiling, clamp usage not allocation" shape as FDNReverb's
// kMaxPreDelaySeconds/MultiTapDelay's kMaxDelaySeconds. A "few seconds"
// comfortably covers the default (0 + 0.15s) with headroom for a caller
// that wants to scan further back.
constexpr float kMaxCaptureSeconds = 5.0f;

// Resolution of the precomputed window table - resampled by grain length
// at trigger time (sampleWindow()), not recomputed per grain/per sample.
constexpr int kWindowTableSize = 1024;

// Tukey taper ratio: the fraction of the window's width spent in the
// cosine taper at each edge (the rest is a flat plateau at unity). 0.5,
// not a pure Hann (taper ratio 1.0): a pure Hann's taper starts
// attenuating from sample 0, softening transients inside short grains
// (10-40ms) even at the grain's own center - undesirable for a cloud
// that's supposed to read as many discrete sonic events, not one smeared
// blur. Fixed, not exposed as a parameter (the spec asks for one window
// shape, not a family of them).
constexpr float kTukeyAlpha = 0.5f;

constexpr float kMinGrainSizeMs = 10.0f;
constexpr float kMaxGrainSizeMs = 200.0f;

// Ceiling on pitch scatter (+-cents) - unlike the other parameters, this
// one has no natural playback-rate ceiling of its own (rate = 2^(cents/
// 1200) grows unboundedly with cents), which would otherwise let
// grain.readPos jump by more than one bufLen per sample in process(),
// defeating its single-subtraction wrap. +-2 octaves (2400 cents) is
// already far beyond "gentle" and well past anything the zero-config
// default (15 cents) suggests.
constexpr float kMaxPitchScatter = 2400.0f;

// Ceiling on the scheduler's per-sample phase step. Deliberately well
// below the point where density alone starts to matter more than
// kMaxSimultaneousGrains: at density * grainDuration significantly above
// ~1 overlapping grain, and default (narrow, "gentle") pitch/scan
// scatter, the ensemble of overlapping windowed grains starts to
// reconstruct something close to the original input via straightforward
// overlap-add math - sounding like a smeared echo of it, not a cloud of
// discrete grains, regardless of how many taps are available. 100/sec
// keeps average overlap at kMaxGrainSizeMs (200ms) to 100*0.2 = 20
// simultaneous grains - dense, but still short of that reconstruction
// regime for the *default* scatter settings; a caller that wants denser
// clouds than that without the result reading as an echo needs to raise
// pitchScatter/scanJitter too, so simultaneous grains stop closely
// resembling each other, not just raise density on its own.
constexpr float kMaxDensity = 100.0f;

}

GranularEngine::GranularEngine(int sampleRate)
  : sampleRate_(sampleRate), scatterRng_(0x2545f491u) {
  captureBuffer_.assign(static_cast<size_t>(kMaxCaptureSeconds * static_cast<float>(sampleRate)) + 1, 0.0f);

  windowTable_.resize(static_cast<size_t>(kWindowTableSize));
  for (int n = 0; n < kWindowTableSize; n++) {
    float x = static_cast<float>(n) / static_cast<float>(kWindowTableSize - 1);
    float w;
    if (x < kTukeyAlpha * 0.5f) {
      w = 0.5f * (1.0f + cosf(static_cast<float>(M_PI) * (2.0f * x / kTukeyAlpha - 1.0f)));
    } else if (x > 1.0f - kTukeyAlpha * 0.5f) {
      w = 0.5f * (1.0f + cosf(static_cast<float>(M_PI) * (2.0f * x / kTukeyAlpha - 2.0f / kTukeyAlpha + 1.0f)));
    } else {
      w = 1.0f;
    }
    windowTable_[static_cast<size_t>(n)] = w;
  }

  setParameters(kDefaultGrainSizeMs, kDefaultDensity, kDefaultScanPosition, kDefaultScanJitter,
                kDefaultPitchScatter, kDefaultAmplitudeJitter);
}

void
GranularEngine::setParameters(float grainSizeMs, float densityPerSec,
                               float scanPositionSeconds, float scanJitterSeconds,
                               float pitchScatterCents, float amplitudeJitter) {
  if (grainSizeMs < kMinGrainSizeMs) grainSizeMs = kMinGrainSizeMs;
  if (grainSizeMs > kMaxGrainSizeMs) grainSizeMs = kMaxGrainSizeMs;
  if (densityPerSec < 0.0f) densityPerSec = 0.0f;
  if (densityPerSec > kMaxDensity) densityPerSec = kMaxDensity;
  if (scanPositionSeconds < 0.0f) scanPositionSeconds = 0.0f;
  if (scanJitterSeconds < 0.0f) scanJitterSeconds = 0.0f;
  if (pitchScatterCents < 0.0f) pitchScatterCents = 0.0f;
  if (pitchScatterCents > kMaxPitchScatter) pitchScatterCents = kMaxPitchScatter;
  if (amplitudeJitter < 0.0f) amplitudeJitter = 0.0f;
  if (amplitudeJitter > 1.0f) amplitudeJitter = 1.0f;

  grainSizeMs_ = grainSizeMs;
  densityPerSec_ = densityPerSec;
  scanPositionSeconds_ = scanPositionSeconds;
  scanJitterSeconds_ = scanJitterSeconds;
  pitchScatterCents_ = pitchScatterCents;
  amplitudeJitter_ = amplitudeJitter;
}

float
GranularEngine::sampleWindow(float t) const {
  float idx = t * static_cast<float>(windowTable_.size() - 1);
  int i0 = static_cast<int>(idx);
  int i1 = i0 + 1 < static_cast<int>(windowTable_.size()) ? i0 + 1 : i0;
  float frac = idx - static_cast<float>(i0);
  float w0 = windowTable_[static_cast<size_t>(i0)];
  float w1 = windowTable_[static_cast<size_t>(i1)];
  return w0 + (w1 - w0) * frac;
}

int
GranularEngine::findFreeOrStealGrainSlot() {
  for (int g = 0; g < kMaxSimultaneousGrains; g++) {
    if (!grains_[static_cast<size_t>(g)].active) return g;
  }

  // Every slot is occupied (worst-case density) - steal the grain closest
  // to its own natural end (highest age/length completion ratio, not
  // just highest raw age, since grain lengths can differ) rather than
  // skip scheduling: graceful degradation, no silently dropped trigger.
  int best = 0;
  float bestRatio = -1.0f;
  for (int g = 0; g < kMaxSimultaneousGrains; g++) {
    auto & grain = grains_[static_cast<size_t>(g)];
    float ratio = grain.lengthSamples > 0 ? static_cast<float>(grain.age) / static_cast<float>(grain.lengthSamples) : 1.0f;
    if (ratio > bestRatio) {
      bestRatio = ratio;
      best = g;
    }
  }
  return best;
}

void
GranularEngine::triggerGrain() {
  int bufLen = static_cast<int>(captureBuffer_.size());

  int lengthSamples = static_cast<int>(grainSizeMs_ * 0.001f * static_cast<float>(getSampleRate()));
  if (lengthSamples < 1) lengthSamples = 1;

  // Pitch scatter computed before lookback (not after, despite reading
  // less naturally in parameter order) - a grain's own playback rate
  // determines the minimum lookback it needs, below.
  float cents = scatterRng_.next() * pitchScatterCents_;
  float rate = powf(2.0f, cents / 1200.0f);

  // How far back (seconds) from the live write head to start reading -
  // base + uniform jitter, clamped to what the buffer can actually hold.
  float scanSeconds = scanPositionSeconds_ + scatterRng_.next() * scanJitterSeconds_;
  if (scanSeconds < 0.0f) scanSeconds = 0.0f;

  // A grain playing faster than real time (rate > 1) closes the distance
  // to the live write head by (rate-1) samples every sample it plays; a
  // lookback smaller than (rate-1)*lengthSamples lets it catch up to (and
  // read past) the write head before its own natural end, landing on
  // either not-yet-written silence (early in a session) or stale,
  // pre-wrap content (once the buffer has wrapped at least once) -
  // reported as a real symptom even at the default (gentle, +-15 cents)
  // pitch scatter, since roughly half of all grains draw an
  // (unclamped-negative, so clamped to exactly 0) lookback in the first
  // place, and *any* rate above 1.0 eventually drifts past 0. Flooring
  // lookback at this worst case makes this impossible regardless of how
  // small scanPosition/scanJitter are configured, without needing to
  // touch scanJitter's own distribution.
  //
  // kCatchUpMarginSamples covers exactly one thing: lookbackSamples below
  // truncates scanSeconds*sampleRate to an int, which can lose just under
  // one full sample - a single flat sample count is enough for that
  // regardless of sample rate, grain length, or pitch scatter, since it's
  // a one-time truncation, not an accumulated error (Grain::readPos is
  // double specifically so that accumulating += grain.rate every sample
  // for a grain's whole life never needs *its own* margin here, at any
  // sample rate --samplerate can be set to - see readPos's own comment).
  float minScanSeconds = 0.0f;
  if (rate > 1.0f) {
    constexpr float kCatchUpMarginSamples = 4.0f;
    minScanSeconds = (rate - 1.0f) * static_cast<float>(lengthSamples) / static_cast<float>(getSampleRate())
                    + kCatchUpMarginSamples / static_cast<float>(getSampleRate());
    if (scanSeconds < minScanSeconds) scanSeconds = minScanSeconds;
  }

  float maxScanSeconds = static_cast<float>(bufLen - 1) / static_cast<float>(getSampleRate());
  if (scanSeconds > maxScanSeconds) scanSeconds = maxScanSeconds;

  // Also bound to how much real history actually exists yet
  // (capturedSamples_'s own doc comment) - otherwise, early in playback,
  // or whenever the requested scan depth exceeds what's been captured so
  // far, this would read from a still-zero-initialized stretch of the
  // buffer instead of real audio. Deliberately *wrapped* (fmodf), not
  // clamped to the single available-seconds ceiling: a hard clamp would
  // send every grain whose draw exceeds what's captured to read from
  // exactly the same oldest available sample - many grains piling onto
  // one identical point sounds like a repeated echo fragment, not a
  // cloud. Wrapping keeps different grains' draws landing at different
  // remainders, so the cloud stays diverse even before there's a full
  // scanPosition+scanJitter's worth of real history to draw from (which,
  // for the default scanJitter of 0.15s, is only ever true for the first
  // 150ms of playback).
  //
  // The catch-up floor above is a *safety* requirement, not an artistic
  // one, so it must never be wrapped away by this clamp the way the
  // caller's own requested scanSeconds can be: if even minScanSeconds
  // exceeds what's actually been captured (only reachable in the first
  // fraction of a second of a session, or shortly after freeze/unfreeze,
  // and only for a rate significantly above 1.0 - default pitch scatter
  // never gets close), this grain cannot run safely yet at all - skip it
  // outright (no slot consumed, nothing else about this call's state
  // touched) rather than either read unsafe territory or silently defeat
  // the floor. The scheduler's own cadence (process()'s schedulerPhase_)
  // is unaffected either way - only whether *this* attempt produces an
  // audible grain.
  float availableSeconds = static_cast<float>(capturedSamples_) / static_cast<float>(getSampleRate());
  if (minScanSeconds > availableSeconds) return;

  if (scanSeconds > availableSeconds) {
    scanSeconds = availableSeconds > 0.0f ? fmodf(scanSeconds, availableSeconds) : 0.0f;
  }
  int lookbackSamples = static_cast<int>(scanSeconds * static_cast<float>(getSampleRate()));

  // captureWritePos_ (this method is called from process(), after the
  // current sample's capture write and increment) points to the *next*
  // position to be written, not the one just written - the most
  // recently captured sample is captureWritePos_ - 1. Without the "- 1"
  // here, lookbackSamples == 0 (a common draw: roughly half of all
  // grains land there, since negative jitter draws clamp to 0) would
  // start the grain reading one sample into not-yet-written territory
  // instead of the freshest real audio.
  int startPos = captureWritePos_ - 1 - lookbackSamples;
  startPos %= bufLen;
  if (startPos < 0) startPos += bufLen;

  // Slot acquisition (and any stealing it does) deliberately happens only
  // after every reason to skip this trigger has already been ruled out
  // above - an attempt that ends up not running shouldn't disturb an
  // existing, currently-sounding grain.
  int slot = findFreeOrStealGrainSlot();
  auto & grain = grains_[static_cast<size_t>(slot)];

  float amplitude = 1.0f + scatterRng_.next() * amplitudeJitter_;
  if (amplitude < 0.0f) amplitude = 0.0f;

  grain.active = true;
  grain.readPos = static_cast<double>(startPos);
  grain.lengthSamples = lengthSamples;
  grain.age = 0;
  grain.rate = rate;
  grain.amplitude = amplitude;
  grain.generation++;

  grainsTriggeredForTest_++;
}

void
GranularEngine::process(const float * monoInput, int frames) {
  for (auto & tap : taps_) {
    if (static_cast<int>(tap.size()) != frames) tap.resize(static_cast<size_t>(frames));
  }

  int bufLen = static_cast<int>(captureBuffer_.size());
  float samplesPerGrainStep = densityPerSec_ > 0.0f ? static_cast<float>(getSampleRate()) / densityPerSec_ : 0.0f;

  for (int i = 0; i < frames; i++) {
    // Capture - skipped entirely while frozen, so the buffer's contents
    // are held exactly as they were at the moment freeze engaged (see
    // setFreeze()'s own doc comment for why this needs no fade on either
    // transition).
    if (!freeze_) {
      captureBuffer_[static_cast<size_t>(captureWritePos_)] = monoInput[i];
      captureWritePos_++;
      if (captureWritePos_ >= bufLen) captureWritePos_ = 0;
      if (capturedSamples_ < bufLen) capturedSamples_++;
    }

    // Scheduler: a phase accumulator in samples, not seconds, so it never
    // needs a large-number modulo - crosses samplesPerGrainStep exactly
    // once per average grain interval regardless of how long ago it last
    // fired.
    if (densityPerSec_ > 0.0f) {
      schedulerPhase_ += 1.0f;
      if (schedulerPhase_ >= samplesPerGrainStep) {
        schedulerPhase_ -= samplesPerGrainStep;
        triggerGrain();
      }
    }

    for (int g = 0; g < kMaxSimultaneousGrains; g++) {
      auto & grain = grains_[static_cast<size_t>(g)];
      auto & tap = taps_[static_cast<size_t>(g)];
      if (!grain.active) {
        tap[static_cast<size_t>(i)] = 0.0f;
        continue;
      }

      float t = grain.lengthSamples > 1 ? static_cast<float>(grain.age) / static_cast<float>(grain.lengthSamples - 1) : 1.0f;
      float win = sampleWindow(t);

      int i0 = static_cast<int>(grain.readPos);
      float frac = static_cast<float>(grain.readPos - static_cast<double>(i0));
      int i0m = i0 % bufLen;
      int i1m = i0m + 1 < bufLen ? i0m + 1 : 0;
      float s0 = captureBuffer_[static_cast<size_t>(i0m)];
      float s1 = captureBuffer_[static_cast<size_t>(i1m)];
      float sample = s0 + (s1 - s0) * frac;

      tap[static_cast<size_t>(i)] = sample * win * grain.amplitude;

      // readPos must be wrapped explicitly here, not left to grow past
      // bufLen and rely solely on the read side's `i0 % bufLen` - that
      // modulo keeps the array access in-bounds, but readPos itself
      // silently drifting past bufLen falls out of sync with
      // captureWritePos_ (which *does* wrap, exactly once per
      // kMaxCaptureSeconds). A grain triggered with a small lookback (its
      // startPos close to the write head) whose life happens to straddle
      // the write head's own wrap point ends up, once its raw readPos
      // exceeds bufLen, reading from content just ahead of the
      // (now-wrapped) write head - the *oldest*, not-yet-overwritten data
      // in the whole buffer, up to kMaxCaptureSeconds old - rather than
      // continuing to track "just behind now". Wrapping readPos here
      // keeps it crossing the same boundary in lockstep with
      // captureWritePos_, preserving the intended lookback distance
      // through the wrap. A single conditional subtraction (not fmodf) is
      // enough since grain.rate is always well under bufLen per sample.
      grain.readPos += static_cast<double>(grain.rate);
      if (grain.readPos >= static_cast<double>(bufLen)) grain.readPos -= static_cast<double>(bufLen);

      grain.age++;
      if (grain.age >= grain.lengthSamples) grain.active = false;
    }
  }
}
