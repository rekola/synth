#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <memory>

using namespace std;

// The transport's position depends only on how many frames have played,
// never on how they were split into blocks - a block spanning several
// rows advances exactly as far as those rows' own samples.

namespace {

struct Transport {
  Song song;
  ChannelConfiguration config{44100, 1};
  unique_ptr<Mixer> mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state{config};

  Transport() {
    song.setTimeSignature(TimeSignature{1, 4});
    auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    auto arrangement = song.getArrangement();
    for (int row = 0; row < 64; row++) arrangement.setNote(row, track_id, 0, Note(60, 100));
    state.initialize(song);
    state.setIsPlaying(true);
  }

  int rowSamples() const { return config.getSampleInterval(song.getTempo()); }

  void render(int frames, int block) {
    for (int done = 0; done < frames; done += block) state.renderBlock(min(block, frames - done), song, *mixer);
  }
};

}

TEST(a_block_of_whole_rows_ends_exactly_on_a_row_boundary) {
  Transport t;
  t.render(5 * t.rowSamples(), 5 * t.rowSamples());
  CHECK(t.state.getAbsolutePosition() == 5);
  CHECK(t.state.getSamplePos() == 0);
}

TEST(the_transport_position_does_not_depend_on_the_block_size) {
  Transport one_block, small_blocks, odd_blocks;
  int frames = 7 * one_block.rowSamples() / 2; // three and a half rows
  one_block.render(frames, frames);
  small_blocks.render(frames, 64);
  odd_blocks.render(frames, 997);
  CHECK(one_block.state.getAbsolutePosition() == 3);
  CHECK(one_block.state.getSamplePos() == frames - 3 * one_block.rowSamples());
  CHECK(small_blocks.state.getAbsolutePosition() == one_block.state.getAbsolutePosition());
  CHECK(small_blocks.state.getSamplePos() == one_block.state.getSamplePos());
  CHECK(odd_blocks.state.getAbsolutePosition() == one_block.state.getAbsolutePosition());
  CHECK(odd_blocks.state.getSamplePos() == one_block.state.getSamplePos());
}
