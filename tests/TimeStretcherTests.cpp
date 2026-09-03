#include "TestFramework.h"

#include "../src/audio/TimeStretcher.h"

#include <cmath>

using namespace std;

namespace {

vector<float> buildSine(int frames, float frequency, int sample_rate) {
  vector<float> out(static_cast<size_t>(frames));
  for (int i = 0; i < frames; i++) {
    out[static_cast<size_t>(i)] = sinf(2.0f * static_cast<float>(M_PI) * frequency * static_cast<float>(i) / static_cast<float>(sample_rate));
  }
  return out;
}

// 2 per cycle for a sine - a rough, cheap proxy for "what frequency is
// this" without a full FFT, good enough to tell "pitch preserved" apart
// from "pitch shifted along with duration" by a wide margin.
int countZeroCrossings(const vector<float> & signal, size_t begin, size_t end) {
  int count = 0;
  for (size_t i = max(begin, size_t(1)); i < end && i < signal.size(); i++) {
    if ((signal[i - 1] < 0.0f) != (signal[i] < 0.0f)) count++;
  }
  return count;
}

}

TEST(stretch_mono_is_a_no_op_for_degenerate_input) {
  vector<float> input(100, 0.5f);
  CHECK(stretchMono(input, 0, 1.5).size() == input.size());
  CHECK(stretchMono(input, 8000, 0.0).size() == input.size());
  CHECK(stretchMono(input, 8000, -1.0).size() == input.size());
  CHECK(stretchMono(input, 8000, 1.0).size() == input.size()); // ratio 1.0 - unchanged, not run through the algorithm for nothing
  CHECK(stretchMono({}, 8000, 1.5).empty());
}

TEST(stretch_mono_lengthens_duration_for_a_ratio_below_one) {
  int sample_rate = 8000;
  auto input = buildSine(sample_rate * 2, 220.0f, sample_rate); // 2 real seconds

  auto slower = stretchMono(input, sample_rate, 0.5); // half tempo -> roughly double duration
  CHECK(slower.size() > input.size() * 3 / 2);
  CHECK(slower.size() < input.size() * 5 / 2);
}

TEST(stretch_mono_shortens_duration_for_a_ratio_above_one) {
  int sample_rate = 8000;
  auto input = buildSine(sample_rate * 2, 220.0f, sample_rate); // 2 real seconds

  auto faster = stretchMono(input, sample_rate, 2.0); // double tempo -> roughly half duration
  CHECK(faster.size() > input.size() / 4);
  CHECK(faster.size() < input.size() * 3 / 4);
}

// The whole reason this exists rather than reusing Resampler.h for a
// tempo mismatch too (see TimeStretcher.h's own comment): a plain
// resample changes pitch right along with duration, which is wrong for
// "the same recording, just fit to a different tempo." Confirmed here by
// counting zero-crossings over a fixed *real-time* window of the
// stretched output - a naive resample-based stretch would have halved
// the crossing rate (lower pitch) along with doubling the duration;
// genuine tempo-only stretching leaves it close to the original tone's
// own rate.
TEST(stretch_mono_preserves_pitch_rather_than_shifting_it_like_a_plain_resample_would) {
  int sample_rate = 8000;
  float frequency = 440.0f;
  auto input = buildSine(sample_rate * 2, frequency, sample_rate); // 2 real seconds

  auto stretched = stretchMono(input, sample_rate, 0.5); // half tempo -> ~2x duration
  CHECK(stretched.size() > static_cast<size_t>(sample_rate)); // comfortably more than 1 real second of output

  // A one-second window starting a little in, past SoundTouch's own short
  // analysis-window warm-up at the very start of the stream.
  auto window_start = static_cast<size_t>(sample_rate) / 5;
  auto crossings = countZeroCrossings(stretched, window_start, window_start + static_cast<size_t>(sample_rate));
  auto expected = static_cast<int>(2.0f * frequency); // 2 crossings per cycle
  CHECK(crossings > expected * 7 / 10);
  CHECK(crossings < expected * 13 / 10);
}
