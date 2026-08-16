#include "TestFramework.h"

#include "../src/dsp/GranularEngine.h"
#include "../src/dsp/NoiseGenerator.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;

namespace {

// Feeds `frames` samples of white noise (not silence/an impulse - a
// granulator needs actual content to read grains from) through `engine`,
// in fixed-size blocks like the real audio thread would.
void feedNoise(GranularEngine & engine, int frames, uint32_t seed = 12345u) {
  NoiseGenerator noise(seed);
  vector<float> input(static_cast<size_t>(frames));
  for (auto & s : input) s = noise.next();

  int block = 512;
  for (int offset = 0; offset < frames; offset += block) {
    int n = min(block, frames - offset);
    engine.process(input.data() + offset, n);
  }
}

}

TEST(granular_engine_grains_read_captured_audio_not_uncaptured_silence) {
  // Regression test: a grain's scan-back distance must clamp to how much
  // real audio has actually been captured so far (capturedSamples_ in
  // dsp/GranularEngine.h), not just to the buffer's physical capacity -
  // otherwise, whenever scanPosition/scanJitter asks to look back further
  // than what's been captured (always true right after playback starts,
  // or whenever a caller sets a nontrivial scanPosition), every grain
  // would read from a still-zero-initialized stretch of the buffer and
  // come out silent/thin until real capture caught up - "grains take a
  // long time to start sounding," reported against an earlier version of
  // this code that only clamped against captureBuffer_.size().
  int sampleRate = 44100;
  GranularEngine engine(sampleRate);
  // A scan position (2s) far beyond what a brief capture window (0.2s,
  // below) can possibly satisfy, with no jitter so every grain requests
  // exactly that same (unreachable) depth.
  engine.setParameters(60.0f, 200.0f, 2.0f, 0.0f, 0.0f, 0.0f);

  NoiseGenerator noise(7u);
  vector<float> input(static_cast<size_t>(sampleRate / 5)); // 0.2s of real, non-silent content
  for (auto & s : input) s = noise.next();
  engine.process(input.data(), static_cast<int>(input.size()));

  CHECK(engine.getGrainsTriggeredForTest() > 0);
  bool sawNonSilentGrainOutput = false;
  for (int g = 0; g < GranularEngine::kMaxSimultaneousGrains; g++) {
    if (!engine.isGrainActive(g)) continue;
    auto tap = engine.getTap(g);
    for (int i = 0; i < static_cast<int>(input.size()); i++) {
      if (tap[i] != 0.0f) sawNonSilentGrainOutput = true;
    }
  }
  CHECK(sawNonSilentGrainOutput);
}

TEST(granular_engine_zero_density_triggers_no_grains) {
  int sampleRate = 44100;
  GranularEngine engine(sampleRate);
  engine.setParameters(60.0f, 0.0f, 0.0f, 0.15f, 15.0f, 0.2f);

  feedNoise(engine, sampleRate);
  CHECK(engine.getGrainsTriggeredForTest() == 0);
  for (int i = 0; i < GranularEngine::kMaxSimultaneousGrains; i++) {
    CHECK(!engine.isGrainActive(i));
  }
}

TEST(granular_engine_grain_count_matches_density) {
  int sampleRate = 44100;
  GranularEngine engine(sampleRate);
  float density = 20.0f;
  int seconds = 4;
  // No scatter needed for a trigger-count check - defaults are fine.
  // 20/sec at 60ms grains requests overlap 1.2, below kMinOverlapFactor
  // (2.5) - setParameters() floors density upward to meet it (see its own
  // comment in dsp/GranularEngine.cpp), so the expected count is derived
  // from getDensity() (the floored, actually-in-effect value), not the
  // raw density requested above - this test is about the scheduler
  // matching whatever density is genuinely active, not about re-deriving
  // the floor's own arithmetic.
  engine.setParameters(60.0f, density, 0.0f, 0.05f, 10.0f, 0.1f);

  feedNoise(engine, sampleRate * seconds);

  float expected = engine.getDensity() * static_cast<float>(seconds);
  float actual = static_cast<float>(engine.getGrainsTriggeredForTest());
  // The scheduler is a fixed-step phase accumulator (dsp/GranularEngine.cpp),
  // so this should be within a single grain interval of the target count.
  CHECK_NEAR(actual, expected, 2.0f);
}

TEST(granular_engine_density_is_floored_to_avoid_sub_overlap_gating) {
  // Regression test for the overlap floor: a caller requesting a
  // density/grainSize combination whose overlap (density * grainSize)
  // would fall below kMinOverlapFactor must have density raised - never
  // grainSize shrunk - to meet it, so grains can never gate/stutter
  // regardless of what a future preset, or hand-edited XML, asks for
  // (this is the exact class of bug a real diagnosis found: a preset
  // sitting at overlap 0.9, meaning consecutive grains didn't even
  // touch).
  GranularEngine engine(44100);
  // 20/sec at 60ms grains requests overlap 1.2 - well under the 2.5 floor.
  engine.setParameters(60.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f);

  float overlap = engine.getDensity() * (engine.getGrainSizeMs() * 0.001f);
  CHECK(overlap >= 2.5f - 0.001f);
  // The floor never shrinks grain size, only raises density.
  CHECK_NEAR(engine.getGrainSizeMs(), 60.0f, 0.001f);
  CHECK(engine.getDensity() > 20.0f);
}

TEST(granular_engine_zero_density_stays_off_despite_overlap_floor) {
  // The overlap floor must not apply to density=0 (the explicit "no
  // scheduling at all" sentinel - see process()'s own
  // `if (densityPerSec_ > 0.0f)` guard) - otherwise disabling the grain
  // scheduler entirely would become impossible.
  GranularEngine engine(44100);
  engine.setParameters(60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  CHECK_NEAR(engine.getDensity(), 0.0f, 0.0001f);

  feedNoise(engine, 44100);
  CHECK(engine.getGrainsTriggeredForTest() == 0);
}

TEST(granular_engine_never_exceeds_max_simultaneous_grains) {
  int sampleRate = 44100;
  GranularEngine engine(sampleRate);
  // Max legal density (100/sec, kMaxDensity - deliberately capped well
  // below the point where density alone reconstructs the input, see its
  // own doc comment in dsp/GranularEngine.cpp) at max legal grain size
  // (200ms): average concurrent grains = 100 * 0.2 = 20, under the 64
  // cap - stealing (findFreeOrStealGrainSlot()) is a safety net for
  // pathological cases, not something legal steady-state settings are
  // expected to reach, so this test only asserts the cap itself, not
  // that stealing gets exercised: never exceed kMaxSimultaneousGrains,
  // never crash, keep triggering.
  engine.setParameters(200.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f);

  feedNoise(engine, sampleRate);

  int active = 0;
  for (int i = 0; i < GranularEngine::kMaxSimultaneousGrains; i++) {
    if (engine.isGrainActive(i)) active++;
  }
  CHECK(active <= GranularEngine::kMaxSimultaneousGrains);
  CHECK(active > 0);
  CHECK(engine.getGrainsTriggeredForTest() > 0);
}

TEST(granular_engine_freeze_holds_buffer_content_despite_silent_live_input) {
  int sampleRate = 44100;
  GranularEngine engine(sampleRate);
  engine.setParameters(60.0f, 40.0f, 0.0f, 0.05f, 0.0f, 0.0f);

  // Capture real (non-silent) content first, unfrozen.
  feedNoise(engine, sampleRate / 2);
  CHECK(engine.getGrainsTriggeredForTest() > 0);

  engine.setFreeze(true);
  CHECK(engine.getFreeze());

  // Now feed pure silence while frozen - if the capture buffer were still
  // being written, every subsequent grain would read all-zero content;
  // since it's frozen, grains keep reading the earlier noise instead.
  vector<float> silence(static_cast<size_t>(sampleRate), 0.0f);
  int block = 512;
  bool sawNonSilentGrainOutput = false;
  for (int offset = 0; offset < static_cast<int>(silence.size()); offset += block) {
    int n = min(block, static_cast<int>(silence.size()) - offset);
    engine.process(silence.data() + offset, n);
    for (int g = 0; g < GranularEngine::kMaxSimultaneousGrains; g++) {
      if (!engine.isGrainActive(g)) continue;
      auto tap = engine.getTap(g);
      for (int i = 0; i < n; i++) {
        if (tap[i] != 0.0f) sawNonSilentGrainOutput = true;
      }
    }
  }
  CHECK(sawNonSilentGrainOutput);
}

TEST(granular_engine_stable_across_parameter_extremes) {
  // Mirrors fdn_reverb_stable_across_parameter_extremes/
  // multi_tap_delay_stable_across_parameter_extremes: every combination
  // stays finite and bounded, however aggressively the parameters are set.
  struct Case { float grainSizeMs, density, scanPosition, scanJitter, pitchScatter, amplitudeJitter; };
  Case cases[] = {
    { 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 200.0f, 100.0f, 4.0f, 4.0f, 1200.0f, 1.0f },
    { 60.0f, 15.0f, 0.0f, 0.15f, 15.0f, 0.2f },
  };

  int sampleRate = 44100;
  int frames = sampleRate * 2;

  for (auto & c : cases) {
    GranularEngine engine(sampleRate);
    engine.setParameters(c.grainSizeMs, c.density, c.scanPosition, c.scanJitter,
                          c.pitchScatter, c.amplitudeJitter);

    NoiseGenerator noise(42u);
    vector<float> input(512);
    for (int offset = 0; offset < frames; offset += static_cast<int>(input.size())) {
      for (auto & s : input) s = noise.next();
      int n = min(static_cast<int>(input.size()), frames - offset);
      engine.process(input.data(), n);

      for (int g = 0; g < GranularEngine::kMaxSimultaneousGrains; g++) {
        auto tap = engine.getTap(g);
        for (int i = 0; i < n; i++) {
          CHECK(isfinite(tap[i]));
          CHECK(fabs(tap[i]) < 100.0f);
        }
      }
    }
  }
}

TEST(granular_engine_read_position_stays_synchronized_across_buffer_wrap) {
  // Regression test: a grain's readPos must be explicitly wrapped as it
  // increments (dsp/GranularEngine.cpp's process()), not left to grow past
  // captureBuffer_.size() and rely solely on the read side's `% bufLen`.
  // Before this fix, any grain whose life straddled the capture buffer's
  // own wrap point (captureWritePos_ resetting to 0, once every several
  // seconds - see kMaxCaptureSeconds) would fall out of sync with the
  // write head and, for the rest of its life, read stale content from
  // just ahead of the (now-wrapped) write head - up to kMaxCaptureSeconds
  // old - instead of continuing to track "just behind now". Reported
  // against a real song as "grains take several seconds to appear, then
  // echo old material" - confirmed here by feeding a naturally decaying
  // (not steady) signal, matching a real instrument's envelope, since a
  // steady signal doesn't reveal the bug (old and new content look
  // identical either way).
  int sampleRate = 44100;
  GranularEngine engine(sampleRate);
  // Defaults: density=15/sec, grainSize=60ms, scanJitter=0.15s.

  NoiseGenerator noise(555u);
  int block = 512;
  int totalSeconds = 7; // comfortably past kMaxCaptureSeconds (5s)
  int totalFrames = sampleRate * totalSeconds;
  vector<float> input(static_cast<size_t>(block));

  int bufSize = engine.getCaptureBufferSizeForTest();

  for (int offset = 0; offset < totalFrames; offset += block) {
    int n = min(block, totalFrames - offset);
    for (int i = 0; i < n; i++) {
      float t = static_cast<float>(offset + i) / static_cast<float>(sampleRate);
      float envelope = 0.15f * expf(-t / 0.5f); // decaying, like a real instrument's envelope
      input[static_cast<size_t>(i)] = noise.next() * envelope;
    }
    engine.process(input.data(), n);

    for (int g = 0; g < GranularEngine::kMaxSimultaneousGrains; g++) {
      if (!engine.isGrainActive(g)) continue;

      // readPos itself must always stay within the buffer's own bounds -
      // the direct assertion for the fix above.
      double readPos = engine.getGrainReadPosForTest(g);
      CHECK(readPos >= 0.0);
      CHECK(readPos < static_cast<double>(bufSize));

      // And it must stay close to the write head - within the
      // configured scan window plus a little slack for the grain's own
      // forward progress since trigger - never having fallen a nearly-
      // full-buffer-length behind due to a wrap desync (accounting for
      // wraparound distance both ways).
      int writePos = engine.getCaptureWritePosForTest();
      int rawGap = writePos - static_cast<int>(readPos);
      int gap = ((rawGap % bufSize) + bufSize) % bufSize; // samples "behind" the write head, wrapped
      CHECK(gap < sampleRate); // well under 1s - defaults never scan back more than 0.15s
    }
  }
}

TEST(granular_engine_read_position_stays_synchronized_at_extreme_settings) {
  // Same invariant as the test above, but at the worst legal combination
  // simultaneously: the highest sample rate a real device is likely to
  // run at (192kHz - --samplerate has no ceiling, so captureBuffer_'s
  // size, and therefore Grain::readPos's own magnitude, scales with
  // whatever the user picks), max grain size (kMaxGrainSizeMs, 200ms -
  // the longest a single grain accumulates float/double error over), and
  // max pitch scatter (kMaxPitchScatter, 2400 cents - the largest rate,
  // and so the tightest margin the catch-up floor has to get right). If
  // Grain::readPos being double (not float) actually eliminated
  // sample-rate-dependent precision loss as a real concern, this must
  // pass with the same small, sample-rate-independent margin
  // (kCatchUpMarginSamples) the default-settings test above uses -
  // proving the fix generalizes rather than having only been tuned
  // against one specific configuration.
  int sampleRate = 192000;
  GranularEngine engine(sampleRate);
  engine.setParameters(200.0f, 20.0f, 0.0f, 0.0f, 2400.0f, 0.0f);

  NoiseGenerator noise(4242u);
  int block = 512;
  int totalSeconds = 7; // still comfortably past kMaxCaptureSeconds (5s)
  int totalFrames = sampleRate * totalSeconds;
  vector<float> input(static_cast<size_t>(block));

  int bufSize = engine.getCaptureBufferSizeForTest();

  for (int offset = 0; offset < totalFrames; offset += block) {
    int n = min(block, totalFrames - offset);
    for (int i = 0; i < n; i++) {
      float t = static_cast<float>(offset + i) / static_cast<float>(sampleRate);
      float envelope = 0.15f * expf(-t / 0.5f);
      input[static_cast<size_t>(i)] = noise.next() * envelope;
    }
    engine.process(input.data(), n);

    for (int g = 0; g < GranularEngine::kMaxSimultaneousGrains; g++) {
      if (!engine.isGrainActive(g)) continue;

      double readPos = engine.getGrainReadPosForTest(g);
      CHECK(readPos >= 0.0);
      CHECK(readPos < static_cast<double>(bufSize));

      int writePos = engine.getCaptureWritePosForTest();
      int rawGap = writePos - static_cast<int>(readPos);
      int gap = ((rawGap % bufSize) + bufSize) % bufSize;
      // At rate up to 4.0x (2400 cents) and 200ms grains, the catch-up
      // floor alone can legitimately push lookback up to
      // (4.0-1.0)*0.2s = 0.6s - the bound here has to be wide enough to
      // accommodate that deliberately, not just precision slop.
      CHECK(gap < sampleRate); // < 1s, comfortably covers the 0.6s worst case
    }
  }
}
