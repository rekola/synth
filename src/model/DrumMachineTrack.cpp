#include "DrumMachineTrack.h"

#include "Song.h"
#include "Pattern.h"
#include "../state/InstrumentTrackState.h"
#include "../instruments/DrumRankTable.h"

#include <algorithm>

using namespace std;

namespace {

// Runtime counterpart of DrumMachineTrack, local to this file - nothing
// outside createState() below ever names it (Player.cpp's live-audition
// path and InstrumentTrackState::render() both reach it only through the
// base InstrumentTrackState pointer/reference they already have). Inherits
// InstrumentTrackState rather than bare TrackState because a drum
// machine's emitted notes need exactly the same retriggerVoices()/
// chokeExclusiveClasses()/voices_ machinery a pattern-driven InstrumentTrack
// already gets, reused verbatim rather than reimplementing choke/retrigger.
// Otherwise empty for now: no step-driven note emission is wired in yet -
// this class exists so DrumMachineTrack's createState()/createStateTree()
// state-tree machinery is already exercised before that logic lands.
//
// getInstrumentSource() override mirrors PercussionTrack.cpp's own local
// InstrumentTrackState subclass (a DrumMachineTrack has no instrument_id_/
// pool index either - see DrumMachineTrack.h) - every note plays through
// the pool's one drum kit instead) - kept as its own small override rather
// than sharing a base with that one, since a drum machine isn't a kind of
// percussion track, it just happens to source its sound the same way.
class DrumMachineTrackState : public InstrumentTrackState {
public:
  explicit DrumMachineTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : InstrumentTrackState(channel_config, solo, muted, track_id, -1, position, sends) { }

  const Track * getInstrumentSource(const InstrumentPool & instruments) const override {
    return instruments.getDefaultKitInstrument();
  }
};

}

void
DrumMachineTrack::seedDefaultKit() {
  // The default rock kit: kick (36), snare (38), low/low-mid/high tom
  // (45/47/50), closed/open hi-hat (42/46), crash (49) - addLane() derives
  // lane order via DrumRankTable itself, so this list needn't be
  // pre-sorted. addLane() is itself a safe no-op for any note that
  // already has a lane, so calling this on a track that isn't
  // freshly-constructed just fills in whatever default lanes are still
  // missing rather than duplicating existing ones.
  for (int note : { 36, 38, 45, 47, 50, 42, 46, 49 }) addLane(note);
}

unique_ptr<TrackState>
DrumMachineTrack::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  return make_unique<DrumMachineTrackState>(config, isSolo(), isMuted(), getInternalId(), getPosition(), getSends());
}

bool
DrumMachineTrack::hasLane(int note) const {
  return find(lane_notes_.begin(), lane_notes_.end(), note) != lane_notes_.end();
}

void
DrumMachineTrack::addLane(int note) {
  if (hasLane(note)) return;
  if (static_cast<int>(lane_notes_.size()) >= kMaxLanes) return;
  lane_notes_.push_back(note);
  lane_notes_ = DrumRankTable::orderLanes(move(lane_notes_));
}

void
DrumMachineTrack::removeLane(int note, Song & song) {
  auto it = find(lane_notes_.begin(), lane_notes_.end(), note);
  if (it == lane_notes_.end()) return;
  lane_notes_.erase(it);

  // Step data for this note lives in every scene's own Pattern for this
  // track, not a track-global map any more - deleting the lane has to
  // reach into each one. A scene with nothing for this track at all is
  // simply skipped (getPatternsByTrack()[] would otherwise materialize an
  // empty entry purely to delete from it). By index (not a range-for over
  // getScenes(), which only has a const overload) so each scene resolves
  // through Song::getScene(i)'s own mutable overload.
  auto track_id = getInternalId();
  auto num_scenes = static_cast<int>(song.getScenes().size());
  for (int i = 0; i < num_scenes; i++) {
    auto & patterns = song.getScene(i).getPatternsByTrack();
    auto pattern_it = patterns.find(track_id);
    if (pattern_it == patterns.end()) continue;
    pattern_it->second.deleteNotesWithValue(note);
  }
}

void
DrumMachineTrack::clearAllLanes(Song & song) {
  for (int note : vector<int>(lane_notes_)) removeLane(note, song); // copy: removeLane mutates lane_notes_
}

vector<int>
DrumMachineTrack::getHitNotesForRow(const Pattern & pattern, int pattern_row, int context_length) const {
  vector<int> hits;
  auto row = pattern.getEffectiveRow(pattern_row, context_length);
  auto & notes = pattern.getNotes(row);
  for (int lane_note : lane_notes_) {
    for (auto & note : notes) {
      if (note.isDefined() && !note.isOff() && !note.isAftertouch() && note.getValue() == lane_note) {
        hits.push_back(lane_note);
        break;
      }
    }
  }
  return hits;
}
