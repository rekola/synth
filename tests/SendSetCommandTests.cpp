#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Clip.h"
#include "../src/model/ArrangementOps.h"
#include "../src/state/SongState.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {
  float maxAbs(const float * data, int frames) {
    float m = 0.0f;
    for (int i = 0; i < frames; i++) m = std::max(m, std::fabs(data[i]));
    return m;
  }
}

// 0Lxx/0Fxx/0Mxx - see docs/commands.md and Command.h's own comments. An
// absolute set, not a slide (unlike 0Hxx/0Kxx) - what a live-recorded
// fader move needs to capture.
TEST(send_set_command_parses_mnemonic) {
  Command volume("0L00");
  CHECK(volume.isVolumeSet());
  CHECK(!volume.isSendASet());
  CHECK(!volume.isSendBSet());

  Command send_a("0F00");
  CHECK(send_a.isSendASet());
  CHECK(!send_a.isVolumeSet());

  Command send_b("0M00");
  CHECK(send_b.isSendBSet());
  CHECK(!send_b.isVolumeSet());

  Command unrelated("0K10");
  CHECK(!unrelated.isVolumeSet() && !unrelated.isSendASet() && !unrelated.isSendBSet());
}

TEST(send_set_command_decodes_the_full_dB_range) {
  CHECK_NEAR(Command("0L00").getSendSetLinear(), powf(10.0f, -80.0f * 0.05f), 1e-6f); // xx=0 -> -80dB floor
  CHECK_NEAR(Command("0LFF").getSendSetLinear(), 1.0f, 1e-4f); // xx=255 -> 0dB/unity
}

// Command::volumeSet()/sendASet()/sendBSet() - getSendSetLinear()'s own
// inverse, what LaunchpadManager::recordFaderAutomationIfArmed() uses to
// turn a live fader value back into a real command to write. A round
// trip through the 256-step hex quantization can't be bit-exact, but
// should land close.
TEST(send_set_factories_build_a_real_command_that_round_trips) {
  auto volume = Command::volumeSet(0.5f);
  CHECK(volume.isVolumeSet());
  CHECK_NEAR(volume.getSendSetLinear(), 0.5f, 0.01f);

  auto send_a = Command::sendASet(0.1f);
  CHECK(send_a.isSendASet());
  CHECK_NEAR(send_a.getSendSetLinear(), 0.1f, 0.01f);

  auto send_b = Command::sendBSet(1.0f);
  CHECK(send_b.isSendBSet());
  CHECK_NEAR(send_b.getSendSetLinear(), 1.0f, 0.01f);
}

// Values outside the representable -80..0dB range clamp rather than
// wrapping into an unrelated magnitude (makeSendSet()'s own comment) -
// true silence (0.0f linear) clamps to the same -80dB floor xx=00
// already means, and any value above unity clamps to xx=FF/0dB, not an
// out-of-range byte.
TEST(send_set_factories_clamp_out_of_range_values) {
  CHECK(to_string(Command::volumeSet(0.0f)) == "0L00");
  CHECK(to_string(Command::volumeSet(2.0f)) == "0LFF"); // above unity - clamps, doesn't wrap
}

// Full pipeline: a 0Lxx/0Fxx/0Mxx command at a section-level row actually
// sets the track's own live send level before its note-on is even
// triggered in that same row/block - the same live-knob mechanism
// Controller::setTrackSendA()/etc. already use (LeafTrackState::
// setSendA()/etc.), not a separate one, proven audibly via SongState's own
// AuxA sum (the same one the UI's volume meter reads -
// track_state_set_send_a_reaches_an_already_active_voice in
// SendLiveUpdateTests.cpp uses the identical check) rather than reading
// LeafTrackState's own protected send fields directly.
TEST(send_set_command_sets_send_a_over_the_row) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  auto & section = song.addSection();
  section.setNote(0, track_id, 0, Note(60, 100));
  section.setCommand(0, track_id, Command("0F80")); // roughly -14.5dB - Send A defaults to 0/silent otherwise

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(maxAbs(state.getAuxASum().getChannelData(0), row_samples) > 1e-4f);
}

// Same masking-fix guarantee AzimuthSlideTests.cpp's own
// azimuth_slide_command_fires_even_while_a_clip_supplies_the_row_notes
// establishes, for a Set command instead of a Slide one - the section's
// own background command still applies while a real Clip instance is
// what's actually supplying that row's notes.
TEST(send_set_command_fires_even_while_a_clip_supplies_the_row_notes) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip clip(track_id);
  clip.getLeafPattern().setNote(0, 0, Note(60, 100));
  auto clip_id = song.addClip(move(clip)).getId();

  auto & section = song.addSection();
  placeClipInstance(song, section, track_id, 0, 0);
  CHECK(section.getInstance(track_id, 0) == clip_id);
  section.setCommand(0, track_id, Command("0M40")); // Send B - Send B defaults to 0/silent otherwise

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(maxAbs(state.getAuxBSum().getChannelData(0), row_samples) > 1e-4f);
}
