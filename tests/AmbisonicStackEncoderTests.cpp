#include "TestFramework.h"

#include "../src/ambisonic/AmbisonicStackEncoder.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {

// Deterministic, differently-shaped signals and gain sets per block.
float signalAt(int signal, int block, int i) {
  return 0.5f * sinf(0.05f * static_cast<float>(i + 7 * signal + 13 * block) * static_cast<float>(signal + 1));
}

AmbisonicGains gainsFor(int signal, int block) {
  AmbisonicGains g{};
  for (size_t c = 0; c < g.size(); c++) {
    g[c] = 0.3f * sinf(static_cast<float>(c + 1) * 0.7f + static_cast<float>(signal) * 1.3f + static_cast<float>(block) * 0.4f);
  }
  return g;
}

// The stack encoder must equal one AmbisonicVoiceEncoder per signal, for
// every channel count and block length, across gain changes between blocks.
void checkAgainstVoiceEncoders(int channels, int signals, const vector<int> & block_lengths) {
  AmbisonicStackEncoder stack;
  vector<AmbisonicVoiceEncoder> separate(static_cast<size_t>(signals));

  for (size_t block = 0; block < block_lengths.size(); block++) {
    const int frames = block_lengths[block];
    const size_t stride = static_cast<size_t>(frames) + 8; // padded rows, like the oscillator's
    vector<float> dry(static_cast<size_t>(signals) * stride, 123.0f); // padding must be ignored
    vector<AmbisonicGains> targets;
    for (int s = 0; s < signals; s++) {
      for (int i = 0; i < frames; i++) dry[static_cast<size_t>(s) * stride + static_cast<size_t>(i)] = signalAt(s, static_cast<int>(block), i);
      targets.push_back(gainsFor(s, static_cast<int>(block)));
    }

    AudioBuffer got(channels, frames), expected(channels, frames);
    got.zero();
    expected.zero();
    stack.encodeBlock(got, dry.data(), stride, targets, frames);
    for (int s = 0; s < signals; s++) {
      separate[static_cast<size_t>(s)].encodeBlock(expected, dry.data() + static_cast<size_t>(s) * stride, frames, targets[static_cast<size_t>(s)]);
    }

    for (int c = 0; c < channels; c++) {
      for (int i = 0; i < frames; i++) CHECK_NEAR(got.getChannelData(c)[i], expected.getChannelData(c)[i], 2e-5f);
    }
  }
}

} // namespace

TEST(ambisonic_stack_encoder_matches_per_signal_encoders) {
  // Full third order, with block lengths that aren't a multiple of the lane width.
  checkAgainstVoiceEncoders(16, 5, { 64, 37, 1, 8, 100 });
  checkAgainstVoiceEncoders(9, 3, { 33, 33, 7 });
  checkAgainstVoiceEncoders(4, 8, { 512, 5 });
  checkAgainstVoiceEncoders(1, 4, { 20, 20 });
  // A channel count between the fixed accumulator widths.
  checkAgainstVoiceEncoders(6, 2, { 45, 45 });
}

TEST(ambisonic_stack_encoder_adds_to_what_is_already_in_the_buffer) {
  AmbisonicStackEncoder stack;
  const int frames = 24;
  vector<float> dry(static_cast<size_t>(frames), 1.0f);
  AmbisonicGains g{};
  g[0] = 0.5f;

  AudioBuffer out(4, frames);
  out.zero();
  for (int i = 0; i < frames; i++) out.getChannelData(0)[i] = 2.0f;
  stack.encodeBlock(out, dry.data(), static_cast<size_t>(frames), { g }, frames);
  for (int i = 0; i < frames; i++) CHECK_NEAR(out.getChannelData(0)[i], 2.5f, 1e-6f);
}
