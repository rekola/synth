#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/Arpeggiator.h"
#include "../src/instruments/Oscillator.h"
#include "../src/state/SongState.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/ActiveVoiceInfo.h"
#include "../src/state/MemoryParameterSource.h"

#include <vector>
#include <algorithm>
#include <unordered_map>

using namespace std;

// SongState::resyncPlayheadAfterStop()'s own regression coverage - the
// SongState-level gating (see its own comment and
// plans/arpeggiator-timing-fixes.md's "round 2" revision) that
// tests/ArpeggiatorStateTests.cpp's own resyncPlayhead() test doesn't
// reach, since that one calls ArpeggiatorState::resyncPlayhead() directly,
// bypassing the "did the position actually move while stopped" decision
// entirely. Player.cpp's real PlaybackControlEvent::STOP/PLAY handlers are
// the actual callers; this drives the same SongState calls directly, the
// same way tests/PatternBreakTests.cpp drives renderBlock()/setIsPlaying()
// directly rather than through the full Player/event-queue machinery.

namespace {

// The note_value(s) currently sounding on `track_id`, off TrackState's own
// public getAllActiveVoices() - the same introspection
// tests/ArpeggiatorStateTests.cpp's activeNoteValues() uses directly on an
// ArpeggiatorState, here driven end-to-end through a real Song/SongState
// instead.
vector<int> activeNoteValues(const SongState & state, int track_id) {
  unordered_map<int, vector<ActiveVoiceInfo> > out;
  state.getAllActiveVoices(out);
  vector<int> values;
  auto it = out.find(track_id);
  if (it != out.end()) for (auto & v : it->second) values.push_back(v.note_value);
  sort(values.begin(), values.end());
  return values;
}

// A short gate relative to noteDuration (a real gap between steps) rather
// than Arpeggiator's own defaults (noteDuration = gate = 1 row, which is
// legato - a step's gate stays open essentially continuously, right up to
// the next step's own trigger - see ArpeggiatorState::resyncIfNothingRinging()'s
// own comment) - both tests below need a real gap to land in: resync only
// actually happens once nothing is ringing, and the "position didn't move"
// test needs to prove the step clock free-ran *through* a gap while
// stopped, not just that it was mid-note the whole time.
unique_ptr<Arpeggiator> makeGappedArpeggiator() {
  auto arp = make_unique<Arpeggiator>();
  MemoryParameterSource params;
  params.set("mode", string("up"));
  params.set("noteDuration", 4);
  params.set("gate", 1);
  params.set("octaves", 0);
  arp->loadParameters(params);
  return arp;
}

}

TEST(resync_playhead_after_stop_leaves_a_resumed_arpeggiator_alone_when_the_position_did_not_move) {
  Song song;
  song.setTempo(240);
  song.setPatternLength(16);
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & arp = song.addTrack(makeGappedArpeggiator());
  int track_id = arp.getInternalId();

  auto & scene0 = song.addScene();
  scene0.setNote(0, track_id, 0, Note(60, 100));
  scene0.setNote(0, track_id, 1, Note(64, 100));
  // Every later row is deliberately left empty.

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);

  int unit = config.getSampleInterval(song.getTempo()); // one row; step = 4 rows, gate = 1 row (see makeGappedArpeggiator())

  state.setIsPlaying(true);
  state.renderBlock(unit / 2, song, *mixer); // step 0 (note 60), well inside its own gate
  CHECK(activeNoteValues(state, track_id) == vector<int>{60});

  // Stop, then simulate real time passing while stopped - the audio
  // thread's own render loop keeps calling renderBlock() every block
  // regardless of isPlaying() (see SongState::renderBlock()'s own
  // comment), which is what lets the arpeggiator's step timer keep
  // advancing through a pause at all. Long enough here for it to have
  // naturally crossed the 4-row step boundary into step 1 on its own,
  // entirely independent of the (frozen, since isPlaying() is false) row
  // position.
  state.setIsPlaying(false);
  state.notePlaybackStopped();
  state.renderBlock(4 * unit, song, *mixer);
  CHECK(activeNoteValues(state, track_id) == vector<int>{64}); // confirms it really did keep stepping while "stopped"

  // Resume from exactly the same position - nothing moved it while
  // stopped, so resyncPlayheadAfterStop() should leave the step clock
  // alone rather than yanking it back to step 0.
  state.setIsPlaying(true);
  state.resyncPlayheadAfterStop();
  CHECK(activeNoteValues(state, track_id) == vector<int>{64}); // unchanged, not reset
}

TEST(resync_playhead_after_stop_resyncs_a_resumed_arpeggiator_when_the_position_moved) {
  Song song;
  song.setTempo(240);
  song.setPatternLength(16);
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & arp = song.addTrack(makeGappedArpeggiator());
  int track_id = arp.getInternalId();

  auto & scene0 = song.addScene();
  scene0.setNote(0, track_id, 0, Note(60, 100));
  scene0.setNote(0, track_id, 1, Note(64, 100));
  // Row 5 is deliberately left empty - see below.

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);

  int unit = config.getSampleInterval(song.getTempo());

  state.setIsPlaying(true);
  state.renderBlock(unit / 2, song, *mixer);
  CHECK(activeNoteValues(state, track_id) == vector<int>{60});

  state.setIsPlaying(false);
  state.notePlaybackStopped();
  state.renderBlock(4 * unit, song, *mixer); // crosses into step 1, same as above
  CHECK(activeNoteValues(state, track_id) == vector<int>{64});

  // Keep simulating stopped time past step 1's own (1-row) gate close, into
  // the gap before step 2 would naturally be due - resyncIfNothingRinging()
  // (ArpeggiatorState.cpp) only actually resyncs once nothing is ringing,
  // so the seek below needs to land here to prove its own mechanism, not
  // rely on a step happening to already be silent for some other reason.
  state.renderBlock(unit, song, *mixer);
  CHECK(activeNoteValues(state, track_id).empty()); // confirms this really is a gap, not still mid-note

  // The playhead is explicitly moved while stopped (Player.cpp's
  // MOVE_POSITION/SET_POSITION - a real seek, e.g. "reset the playhead").
  // Row 5 is empty, so resuming there doesn't also reschedule row 0's own
  // chord and trigger Fix 2's endPatternRow()-driven resync instead - this
  // test isolates resyncPlayheadAfterStop()'s own mechanism.
  state.setPosition(5);

  state.setIsPlaying(true);
  state.resyncPlayheadAfterStop();

  // The very next render re-triggers immediately, back at step 0 - not
  // wherever the free-running cycle above would otherwise have continued
  // from.
  state.renderBlock(1, song, *mixer);
  CHECK(activeNoteValues(state, track_id) == vector<int>{60});
}
