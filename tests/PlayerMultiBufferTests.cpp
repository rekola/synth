#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/state/SongState.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/Controller.h"
#include "../src/playback/Player.h"
#include "../src/playback/PlaybackControlEvent.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {

// Same shape as DrumMachineTrackTests.cpp's own renderRowPeak() - "was
// anything audible" without needing bit-exact waveform comparison.
float peakOf(const AudioBuffer & master, const Mixer & mixer) {
  float peak = 0.0f;
  for (int c = 0; c < mixer.getOutChannels(); c++) {
    auto data = master.getChannelData(c);
    for (int i = 0; i < master.numberOfFrames(); i++) peak = std::max(peak, std::fabs(data[i]));
  }
  return peak;
}

} // namespace

// Player.cpp's PLAY handling demotes whatever was previously the playing
// buffer to isPlaying(false) rather than tearing its SongState down (see
// the per-buffer editing/playback-state plan's Part B) - this is the
// SongState-level guarantee that relies on: a demoted buffer's own
// already-triggered voices must keep sounding (SongState::renderBlock()'s
// isPlaying()-gated block only ever skips *scheduling new notes*, never
// rendering whatever's already active - see its own comment), and a
// shared Mixer must correctly accumulate two independent SongStates'
// output in the same block without one's own renderBlock() call wiping
// out the other's (the reset_mixer parameter).
TEST(demoted_songstate_keeps_ringing_a_held_voice_while_the_new_playing_state_advances) {
  Song song_a;
  song_a.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track_a = song_a.addTrack(make_unique<InstrumentTrack>(0));
  auto & scene_a = song_a.addScene();
  scene_a.setNote(0, track_a.getInternalId(), 0, Note(60, 100)); // never followed by a note-off - held indefinitely

  Song song_b;
  song_b.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track_b = song_b.addTrack(make_unique<InstrumentTrack>(0));
  auto & scene_b = song_b.addScene();
  scene_b.setNote(0, track_b.getInternalId(), 0, Note(67, 100));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state_a(config), state_b(config);
  state_a.initialize(song_a);
  state_b.initialize(song_b);

  int row_samples = config.getSampleInterval(song_a.getTempo());

  // Buffer A starts playing first, triggering its held note.
  state_a.setIsPlaying(true);
  mixer->reset();
  state_a.renderBlock(row_samples, song_a, *mixer, false);
  CHECK(state_a.getVoiceCount() > 0);

  // Buffer B now takes over the "playing" role - mirrors Player.cpp's PLAY
  // handling exactly: A is demoted (isPlaying(false) + notePlaybackStopped()),
  // never torn down.
  state_a.setIsPlaying(false);
  state_a.notePlaybackStopped();
  state_b.setIsPlaying(true);
  state_b.resyncPlayheadAfterStop();

  auto absolute_before = state_a.getAbsolutePosition();

  // Both render into the *same* mixer this block, mirroring Player::play()'s
  // own multi-buffer render loop: one reset(), then every live SongState's
  // own renderBlock(..., false).
  mixer->reset();
  state_a.renderBlock(row_samples, song_a, *mixer, false);
  state_b.renderBlock(row_samples, song_b, *mixer, false);
  auto master = mixer->encode();

  // A's held note is still sounding - it never got a note-off, and
  // isPlaying(false) only stops *new* scheduling, not what's already
  // active - and its own position stayed frozen, since it's no longer the
  // playing buffer.
  CHECK(state_a.getVoiceCount() > 0);
  CHECK(state_a.getAbsolutePosition() == absolute_before);
  CHECK(!state_a.isPlaying());

  // B is now the one actually driving the transport, and its own note has
  // started sounding too.
  CHECK(state_b.isPlaying());
  CHECK(state_b.getVoiceCount() > 0);

  // The shared mixer's combined output carries real audio - proof the
  // reset_mixer=false accumulation captured both sources this block, not
  // just whichever was rendered last.
  CHECK(peakOf(master, *mixer) > 1e-4f);
}

// The bug Player.cpp's stateFor() eager tree-build guards against: a
// freshly-initialize()'d SongState has no child TrackStates yet - they're
// only created lazily, either by Track::getState() (what stateFor() now
// calls explicitly, for every track, right after initialize()) or
// incidentally by SongState::renderBlock()'s own per-track loop (which a
// brand-new per-buffer SongState hasn't had a chance to run even once by
// the moment its very first PLAY_NOTE event arrives). A plain
// state.getChildByInternalId() lookup - all
// Player::handlePlaybackControlEvent()'s PLAY_NOTE/STOP_NOTE/SET_TRACK_*/
// etc. handlers ever do - finds nothing until one of those two has run;
// without stateFor()'s explicit fix, a fresh buffer's very first note
// silently found no track to play at all.
TEST(fresh_songstate_has_no_track_state_until_getstate_or_a_render_builds_it) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  SongState state(config);
  state.initialize(song);

  CHECK(state.getChildByInternalId(track.getInternalId()) == nullptr);
  track.getState(state, state.getSongStructure()); // the fix - Player::stateFor() calls this for every track eagerly
  CHECK(state.getChildByInternalId(track.getInternalId()) != nullptr);
}

// Row navigation while stopped (SET_POSITION/MOVE_POSITION) must not give
// a buffer its own permanent, forever-rendered live SongState just from
// being scrolled through - see Player.h's pending_positions_ comment.
TEST(set_position_on_a_never_sounded_buffer_creates_no_live_state) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  Player player(config, &controller);
  PlaybackControlEvent set_pos(PlaybackControlEvent::SET_POSITION, buffer_name, 40, 0);
  player.handlePlaybackControlEvent(set_pos);

  CHECK(player.getLiveStatePosition(buffer_name) == -1);
}

// A row navigated to before the buffer had any live state must still be
// honored once something actually makes it sound, not silently dropped
// back to row 0.
TEST(a_pending_position_is_applied_once_the_buffer_actually_makes_sound) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & song = controller.getSong();
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  Player player(config, &controller);

  PlaybackControlEvent set_pos(PlaybackControlEvent::SET_POSITION, buffer_name, 40, 0);
  player.handlePlaybackControlEvent(set_pos);
  CHECK(player.getLiveStatePosition(buffer_name) == -1);

  PlaybackControlEvent play_note(PlaybackControlEvent::PLAY_NOTE, buffer_name, track.getInternalId(), 0, 60, 100);
  player.handlePlaybackControlEvent(play_note);

  CHECK(player.getLiveStatePosition(buffer_name) == 40);
}
