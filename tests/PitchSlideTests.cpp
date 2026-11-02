#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/state/SongState.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/Mixer.h"

#include <cmath>

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
