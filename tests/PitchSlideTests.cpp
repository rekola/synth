#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/state/SongState.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/Mixer.h"

#include <cmath>
#include <vector>

using namespace std;

TEST(pitch_slide_command_parses_direction_and_sixteenths) {
  Command up("0U10");
  CHECK(up.isPitchSlide());
  CHECK_NEAR(up.getPitchSlideSteps(), 1.0f, 1e-6f); // 0x10 = one step
  Command down("0D08");
  CHECK(down.isPitchSlide());
  CHECK_NEAR(down.getPitchSlideSteps(), -0.5f, 1e-6f);
  CHECK_NEAR(Command("0U00").getPitchSlideSteps(), 0.0f, 1e-6f);
  CHECK(!Command("YR05").isPitchSlide());
}

namespace {
// Zero crossings of the left channel in the row after a slide row.
int crossingsAfterSlide(const char * cmd) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track.getInternalId(), 0, Note(60, 100));
  if (cmd) arrangement.setCommand(0, track.getInternalId(), Command(cmd));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);
  mixer->encode();
  int crossings = 0;
  for (int block = 0; block < 32; block++) {
    state.renderBlock(row_samples / 4, song, *mixer);
    auto out = mixer->encode();
    const float * left = out.getChannelData(0);
    for (int i = 1; i < out.numberOfFrames(); i++)
      if ((left[i - 1] < 0) != (left[i] < 0)) crossings++;
  }
  return crossings;
}
} // namespace

TEST(pitch_slide_up_and_down_shift_the_held_note_by_the_given_steps) {
  int base = crossingsAfterSlide(nullptr);
  int up = crossingsAfterSlide("0U80");
  int down = crossingsAfterSlide("0D80");
  int edo = 31; // a fresh song's tuning
  // 80 is eight steps.
  float expected = exp2f(8.0f / static_cast<float>(edo));
  CHECK(base > 0);
  CHECK_NEAR(static_cast<float>(up) / static_cast<float>(base), expected, 0.02f);
  CHECK_NEAR(static_cast<float>(down) / static_cast<float>(base), 1.0f / expected, 0.02f);
  CHECK(up > base && down < base);
}

#include "../src/instruments/OscillatorKernel.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/ambisonic/SphericalPosition.h"

namespace {
struct ProbeVoice : OscillatorVoice {
  using OscillatorVoice::OscillatorVoice;
  float startHz() const { return getFrequency(); }
  float endHz() const { return getFrequencyEnd(); }
};
} // namespace

// The kernel's phase is the exact integral of a linearly moving increment.
TEST(oscillator_kernel_slope_follows_the_exact_phase_integral) {
  const int frames = 1000;
  const double phase = 0.25, rate = 0.01, slope = 4e-6;
  std::vector<float> out(oscillator_kernel::paddedFrames(frames), 0.0f);
  oscillator_kernel::mix(WaveformType::SINE, 0.5f, phase, rate, 1.0f, frames, out.data(), nullptr, nullptr, slope);
  float worst = 0.0f;
  for (int n = 0; n < frames; n++) {
    double cycles = phase + rate * n + 0.5 * slope * n * n;
    worst = max(worst, fabsf(out[static_cast<size_t>(n)] - static_cast<float>(sin(2.0 * M_PI * cycles))));
  }
  CHECK(worst < 2e-3f);
}

// A chunk's end pitch is the slide's offset there, exactly.
TEST(pitch_ramp_end_is_the_offset_at_the_chunk_end) {
  ChannelConfiguration config(44100);
  ProbeVoice voice(config, SphericalPosition{0.0f, 1.0f, 0}, 1.0f, WaveformType::SINE, 1.0f, 0.5f);
  voice.playNote(440.0f, 0.6f, 42);
  CHECK(voice.startHz() == 440.0f && voice.endHz() == 440.0f); // no slide: the same path
  voice.setPitchRamp(100.0, 100.0);                            // the voice starts here
  voice.setPitchRamp(100.0, 1300.0);                           // one octave up over the chunk
  CHECK_NEAR(voice.startHz(), 440.0f, 1e-3f);
  CHECK_NEAR(voice.endHz(), 880.0f, 1e-2f);
}

// UFF/DFF over a single chunk spanning the row (so any stepping would be one
// jump): the local frequency follows the glide and ends where it should.
TEST(pitch_slide_is_smooth_within_one_chunk) {
  for (const char * cmd : {"0UFF", "0DFF"}) {
    Song song;
    song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
    auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
    auto arrangement = song.getArrangement();
    arrangement.setNote(0, track.getInternalId(), 0, Note(240, 100));
    arrangement.setCommand(0, track.getInternalId(), Command(cmd));

    ChannelConfiguration config(44100, 1);
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);

    int row = config.getSampleInterval(song.getTempo());
    state.renderBlock(row, song, *mixer);
    auto out = mixer->encode();
    const float * left = out.getChannelData(0);

    // Interpolated rising zero crossings -> instantaneous frequency.
    vector<double> times;
    for (int i = 1; i < out.numberOfFrames(); i++) {
      if (left[i - 1] < 0 && left[i] >= 0) times.push_back(i - 1 + static_cast<double>(-left[i - 1]) / static_cast<double>(left[i] - left[i - 1]));
    }
    CHECK(times.size() > 8);
    double f_first = 44100.0 / (times[1] - times[0]);
    double f_last = 44100.0 / (times.back() - times[times.size() - 2]);
    double total = (cmd[1] == 'U' ? 1.0 : -1.0) * (255.0 / 16.0) / 31.0; // octaves over the row
    CHECK_NEAR(static_cast<float>(log2(f_last / f_first) / total), 1.0f, 0.12f);
    // Each period sits within 2% of the straight glide's local frequency.
    double base = f_first;
    for (size_t k = 1; k + 1 < times.size(); k++) {
      double t_mid = 0.5 * (times[k] + times[k + 1]);
      double expected = base * exp2(total * t_mid / row);
      double got = 44100.0 / (times[k + 1] - times[k]);
      CHECK(fabs(got / expected - 1.0) < 0.03);
    }
  }
}

#include "../src/instruments/FM.h"

// FM keeps its phase across chunks: one ramp over 2N samples equals the same
// ramp cut in two pieces (each piece ending at the next one's first
// increment), sample for sample.
TEST(fm_pitch_ramp_is_continuous_across_chunks) {
  ChannelConfiguration config(44100, 1);
  FM fm;
  auto make = [&]() {
    auto voice = fm.playNote(config, SphericalPosition{0.0f, 1.0f, 0}, SpatialMode::AUTO, Tuning::EDO12, 1.0f, 0.8f, 69, SendLevels{}, NoteCoordinate{});
    return voice;
  };
  const int n = 512;
  const double cents_per_ratio = 1200.0 / std::log(2.0);
  auto cents = [&](double ratio) { return cents_per_ratio * std::log(ratio); };
  // Frequency ratio rising linearly 1 -> 1.5 over 2n samples.
  auto ratio_at = [&](int k) { return 1.0 + 0.5 * k / (2.0 * n); };

  auto whole = make();
  whole->setPitchRamp(0.0, 0.0);
  whole->setPitchRamp(cents(ratio_at(0)), cents(ratio_at(2 * n)));
  auto a = whole->render(2 * n);

  auto cut = make();
  cut->setPitchRamp(0.0, 0.0);
  cut->setPitchRamp(cents(ratio_at(0)), cents(ratio_at(n)));
  auto b1 = cut->render(n);
  cut->setPitchRamp(cents(ratio_at(n)), cents(ratio_at(2 * n)));
  auto b2 = cut->render(n);

  const float * wa = a.getChannelData(0);
  float worst = 0.0f;
  for (int i = 0; i < n; i++) {
    worst = max(worst, fabsf(wa[i] - b1.getChannelData(0)[i]));
    worst = max(worst, fabsf(wa[n + i] - b2.getChannelData(0)[i]));
  }
  CHECK(worst < 1e-3f);
}

// A slide ends where its row does, even inside a longer block: the glide
// must not stretch to the end of the block.
TEST(pitch_slide_ends_at_the_end_of_its_row_inside_a_longer_block) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track.getInternalId(), 0, Note(240, 100));
  arrangement.setCommand(0, track.getInternalId(), Command("0UFF"));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row = config.getSampleInterval(song.getTempo());
  state.renderBlock(2 * row - 1, song, *mixer); // the slide row and almost the next, one block
  auto out = mixer->encode();
  const float * left = out.getChannelData(0);
  vector<double> times;
  for (int i = 1; i < out.numberOfFrames(); i++) {
    if (left[i - 1] < 0 && left[i] >= 0) times.push_back(i - 1 + static_cast<double>(-left[i - 1]) / static_cast<double>(left[i] - left[i - 1]));
  }
  CHECK(times.size() > 8);
  double f_first = 44100.0 / (times[1] - times[0]);
  double f_end = f_first * exp2((255.0 / 16.0) / 31.0);
  int held = 0;
  for (size_t k = 0; k + 1 < times.size(); k++) {
    if (times[k] < row + 50) continue; // after the slide row
    held++;
    CHECK(fabs(44100.0 / (times[k + 1] - times[k]) / f_end - 1.0) < 0.01);
  }
  CHECK(held > 50);
}
