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

// Same shape as PercussionTrackTests.cpp's own renderRowPeak() - "was
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

// Reproduction for a reported regression: percussion note-off stopped
// working. Exercises the real live-input path (Player::handlePlaybackControlEvent()'s
// PLAY_NOTE/STOP_NOTE, through stateFor()'s own eager tree-build), not the
// pattern-driven scheduling loop a different, model-level test already
// covers - this is where a track-state-tree nesting bug would actually show.
TEST(live_note_off_reclaims_the_voice) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & song = controller.getSong();
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  Player player(config, &controller);

  PlaybackControlEvent play_note(PlaybackControlEvent::PLAY_NOTE, buffer_name, track.getInternalId(), 0, 60, 100);
  player.handlePlaybackControlEvent(play_note);

  auto * state = player.getLiveStateForTest(buffer_name);
  CHECK(state != nullptr);
  if (!state) return;
  CHECK(state->getVoiceCount() > 0);

  PlaybackControlEvent stop_note(PlaybackControlEvent::STOP_NOTE, buffer_name, track.getInternalId(), 0, 0, 0);
  player.handlePlaybackControlEvent(stop_note);

  // Voice release/reclaim needs real render blocks to progress past the
  // note-off, same as the model-level lifecycle tests elsewhere.
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  for (int block = 0; block < 200; block++) state->renderBlock(256, song, *mixer);

  CHECK(state->getVoiceCount() == 0);
}

// OutlineView's own instrument-audition path (PlaybackControlEvent::
// PREVIEW_NOTE/PREVIEW_STOP) - a buffer-agnostic voice with no owning
// Track/live SongState at all, unlike PLAY_NOTE above. "Electric Piano" is
// InstrumentProvider's own always-registered default (see its
// constructor), so no song/track/pool entry is needed - PREVIEW_NOTE
// resolves straight off the provider (see PlaybackControlEvent.h's own
// doc comment on why buffer_name is repurposed to carry the name).
TEST(preview_note_sounds_the_named_instrument_and_stop_reclaims_it) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName()); // gives getCurrentSong() something to resolve tuning from

  Player player(config, &controller);

  // Nothing previewing yet - renderPreview() must still hand back a
  // correctly frame-sized, zero-channel buffer (Mixer::accumulate() needs
  // that shape every block, not just once something is sounding).
  auto before = player.renderPreview(256);
  CHECK(before.numberOfChannels() == 0);
  CHECK(before.numberOfFrames() == 256);

  PlaybackControlEvent preview_note(PlaybackControlEvent::PREVIEW_NOTE, "Electric Piano", 60, 100);
  player.handlePlaybackControlEvent(preview_note);

  auto sounding = player.renderPreview(256);
  CHECK(sounding.numberOfChannels() > 0);
  float peak = 0.0f;
  for (int c = 0; c < sounding.numberOfChannels(); c++) {
    auto data = sounding.getChannelData(c);
    for (int i = 0; i < sounding.numberOfFrames(); i++) peak = std::max(peak, std::fabs(data[i]));
  }
  CHECK(peak > 0.0f);

  PlaybackControlEvent preview_stop(PlaybackControlEvent::PREVIEW_STOP);
  player.handlePlaybackControlEvent(preview_stop);

  // A plain (non-SF2) leaf voice's stopNote() cuts instantly (see
  // InstrumentVoice::stopNote()/killNote(), no release tail to wait out) -
  // but the block that renders the now-silenced voice still comes back
  // real-channel-shaped (silent content, not an absent channel - shape and
  // amplitude are independent), same as InstrumentTrackState's own
  // clearFinishedVoices() only dropping a finished voice at the *next*
  // render, never the one that finished it.
  auto just_stopped = player.renderPreview(256);
  CHECK(just_stopped.numberOfChannels() > 0);

  // Reclaimed by the render after that - renderPreview() saw
  // isActive() false and dropped preview_voice_, so this call finds
  // nothing left to render at all.
  auto after_stop = player.renderPreview(256);
  CHECK(after_stop.numberOfChannels() == 0);
}

// A name that resolves to nothing (InstrumentProvider::
// tryGetByLiteralName()/resolvePath() both miss) previews silence rather
// than substituting the provider's own default instrument - see
// PlaybackControlEvent.h's own doc comment on why PREVIEW_NOTE
// deliberately has no such fallback.
TEST(preview_note_with_an_unresolvable_name_previews_silence) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  Player player(config, &controller);

  PlaybackControlEvent preview_note(PlaybackControlEvent::PREVIEW_NOTE, "nothing registered under this string", 60, 100);
  player.handlePlaybackControlEvent(preview_note);

  auto data = player.renderPreview(256);
  CHECK(data.numberOfChannels() == 0);
}

// OutlineView's own Library > Clips groove-preview path (PlaybackControlEvent::
// PREVIEW_GROOVE/PREVIEW_STOP) - unlike PREVIEW_NOTE above, this is a real
// scheduler advancing block by block, spawning a fresh voice for every hit
// as it comes due, looping - not a single ad hoc note. "Waltz" (kick on
// row 0, rim on rows 4 and 8, 12-row/3-4 loop - see GroovePatternLibrary.cpp)
// is used here as a known, fixed reference pattern.
TEST(preview_groove_schedules_its_hits_and_stop_silences_it) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  Player player(config, &controller);
  auto interval = config.getSampleInterval(controller.getSong().getTempo());

  PlaybackControlEvent preview_groove(PlaybackControlEvent::PREVIEW_GROOVE, "Waltz");
  player.handlePlaybackControlEvent(preview_groove);

  // Row 0's kick is scheduled at frame 0 - the very first block already
  // carries real, audible output.
  auto first_row = player.renderPreview(interval);
  CHECK(first_row.numberOfChannels() > 0);
  float peak = 0.0f;
  for (int c = 0; c < first_row.numberOfChannels(); c++) {
    auto data = first_row.getChannelData(c);
    for (int i = 0; i < first_row.numberOfFrames(); i++) peak = std::max(peak, std::fabs(data[i]));
  }
  CHECK(peak > 0.0f);

  // Advancing through the rest of the 12-row loop (the rim hits at rows 4
  // and 8 land somewhere in here) keeps producing real output - proof the
  // scheduler is still firing block after block, not just coasting on row
  // 0's kick's own release tail.
  bool saw_sound_later = false;
  for (int row = 1; row < 12; row++) {
    auto block = player.renderPreview(interval);
    for (int c = 0; c < block.numberOfChannels(); c++) {
      auto data = block.getChannelData(c);
      for (int i = 0; i < block.numberOfFrames(); i++) if (std::fabs(data[i]) != 0.0f) saw_sound_later = true;
    }
  }
  CHECK(saw_sound_later);

  PlaybackControlEvent preview_stop(PlaybackControlEvent::PREVIEW_STOP);
  player.handlePlaybackControlEvent(preview_stop);

  // Whatever was still ringing at the moment of PREVIEW_STOP finishes its
  // own release tail within a handful of blocks - once nothing is left to
  // render, renderPreview() reports a zero-channel buffer (its own
  // "nothing previewing" shape - see its own comment). Checking for that
  // exact shape, rather than a decaying envelope's own asymptotic-to-
  // (never quite exactly)-zero amplitude, is what actually distinguishes
  // "fully reclaimed" from "very quiet" here. A whole loop's worth per
  // lap, across several laps: if the scheduler had somehow kept running
  // instead of genuinely stopping, a later lap would flip back to
  // non-zero once row 0's kick came due again - it never does.
  bool ever_reported_silent = false;
  for (int lap = 0; lap < 8; lap++) {
    auto block = player.renderPreview(interval * 12);
    if (block.numberOfChannels() == 0) ever_reported_silent = true;
    else CHECK(!ever_reported_silent); // once silent, must never sound again
  }
  CHECK(ever_reported_silent);
}

// A name that resolves to nothing (findGroovePattern() misses) previews
// silence rather than crashing or substituting some other pattern.
TEST(preview_groove_with_an_unresolvable_name_previews_silence) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());

  Player player(config, &controller);

  PlaybackControlEvent preview_groove(PlaybackControlEvent::PREVIEW_GROOVE, "nothing registered under this name");
  player.handlePlaybackControlEvent(preview_groove);

  auto data = player.renderPreview(256);
  CHECK(data.numberOfChannels() == 0);
}

// Regression: Controller's own per-buffer local_position_edit_seq_ counts
// every moveEditPosition()/setEditPosition() call, one per
// MOVE_POSITION/SET_POSITION event pushed - however many of those land
// while the buffer is still stateless (ordinary cursor navigation before
// ever pressing Play) must be reflected in the freshly-built SongState's
// own getPositionEditSeq() too, or Controller::receivePlaybackSnapshot()
// treats every real snapshot as stale relative to its own already-higher
// counter and the displayed playhead/info line simply stops updating
// once playback starts (confirmed - this used to always stamp exactly 1
// regardless of how many navigation events actually preceded it).
TEST(pending_position_edit_seq_reflects_every_navigation_event_before_first_sound) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  auto buffer_name = controller.getActiveBufferName();

  auto & song = controller.getSong();
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  Player player(config, &controller);

  // Three navigation events before the buffer has ever made a sound -
  // mirrors three moveEditPosition()/setEditPosition() calls, each
  // bumping Controller's own local_position_edit_seq_ by one.
  PlaybackControlEvent set_pos_1(PlaybackControlEvent::SET_POSITION, buffer_name, 10, 0);
  PlaybackControlEvent set_pos_2(PlaybackControlEvent::SET_POSITION, buffer_name, 20, 0);
  PlaybackControlEvent set_pos_3(PlaybackControlEvent::SET_POSITION, buffer_name, 40, 0);
  player.handlePlaybackControlEvent(set_pos_1);
  player.handlePlaybackControlEvent(set_pos_2);
  player.handlePlaybackControlEvent(set_pos_3);
  CHECK(player.getLiveStatePosition(buffer_name) == -1);

  PlaybackControlEvent play_note(PlaybackControlEvent::PLAY_NOTE, buffer_name, track.getInternalId(), 0, 60, 100);
  player.handlePlaybackControlEvent(play_note);

  CHECK(player.getLiveStatePosition(buffer_name) == 40);
  CHECK(player.getLiveStatePositionEditSeq(buffer_name) == 3);
}
