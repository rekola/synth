#include "PercussionTrack.h"

#include "Song.h"
#include "Pattern.h"
#include "../state/InstrumentTrackState.h"
#include "../instruments/DrumRankTable.h"

#include <algorithm>

using namespace std;

namespace {

// Runtime counterpart of PercussionTrack, local to this file - nothing
// outside createState() below ever names it (Player.cpp's live-audition
// path and InstrumentTrackState::render() both reach it only through the
// base InstrumentTrackState pointer/reference they already have). Identical
// to InstrumentTrackState in every respect except which instrument its
// notes play through: a PercussionTrack has no instrument_id_/pool index of
// its own (see PercussionTrack.h) - every note plays through the pool's one
// drum kit instead (InstrumentPool::getDefaultKitInstrument()). The base
// constructor's `instrument_id` argument is fixed at -1 here since it's
// never read - getInstrumentSource() below ignores instrument_id_ entirely.
class PercussionTrackState : public InstrumentTrackState {
public:
  explicit PercussionTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : InstrumentTrackState(channel_config, solo, muted, track_id, -1, position, sends) { }

  const Track * getInstrumentSource(const InstrumentPool & instruments) const override {
    return instruments.getDefaultKitInstrument();
  }
};

// Each named preset is exactly PercussionTrack::kMaxLanes GM percussion
// notes - a curated *subset of lanes*, not a different sound source
// (every preset still plays through the same pool kit - see
// PercussionTrack.h's own header comment). NONE has none at all - applying
// it is the explicit way back to a plain, lane-less track. addLane()
// derives lane order via DrumRankTable itself, so none of these need to be
// pre-sorted.
const vector<int> & notesForPreset(PercussionTrack::Preset preset) {
  static const vector<int> kNone;
  static const vector<int> kRock = { 36, 38, 45, 47, 50, 42, 46, 49 }; // kick/snare/low+mid+high tom/closed+open hi-hat/crash
  static const vector<int> kLatin = { 60, 61, 63, 64, 65, 69, 56, 75 }; // hi+low bongo/open hi conga/low conga/high timbale/cabasa/cowbell/claves
  static const vector<int> kElectronic = { 36, 40, 39, 42, 46, 56, 49, 51 }; // kick/electric snare/hand clap/closed+open hi-hat/cowbell/crash/ride
  switch (preset) {
  case PercussionTrack::Preset::LATIN: return kLatin;
  case PercussionTrack::Preset::ELECTRONIC: return kElectronic;
  case PercussionTrack::Preset::ROCK: return kRock;
  case PercussionTrack::Preset::NONE: default: return kNone;
  }
}

}

void
PercussionTrack::applyPreset(Preset preset, Song & song) {
  clearAllLanes(song);
  for (int note : notesForPreset(preset)) addLane(note);
}

unique_ptr<TrackState>
PercussionTrack::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  return make_unique<PercussionTrackState>(config, isSolo(), isMuted(), getInternalId(), getPosition(), getSends());
}

bool
PercussionTrack::hasLane(int note) const {
  return find(lane_notes_.begin(), lane_notes_.end(), note) != lane_notes_.end();
}

void
PercussionTrack::addLane(int note) {
  if (hasLane(note)) return;
  if (static_cast<int>(lane_notes_.size()) >= kMaxLanes) return;
  lane_notes_.push_back(note);
  lane_notes_ = DrumRankTable::orderLanes(move(lane_notes_));
}

void
PercussionTrack::removeLane(int note, Song & song) {
  auto it = find(lane_notes_.begin(), lane_notes_.end(), note);
  if (it == lane_notes_.end()) return;
  lane_notes_.erase(it);

  // Step data for this note lives in every section's own Pattern for this
  // track, not a track-global map any more - deleting the lane has to
  // reach into each one. A section with nothing for this track at all is
  // simply skipped (getPatternsByTrack()[] would otherwise materialize an
  // empty entry purely to delete from it). By index (not a range-for over
  // getSections(), which only has a const overload) so each section resolves
  // through Song::getSection(i)'s own mutable overload.
  auto track_id = getInternalId();
  auto num_sections = static_cast<int>(song.getSections().size());
  for (int i = 0; i < num_sections; i++) {
    auto & patterns = song.getSection(i).getPatternsByTrack();
    auto pattern_it = patterns.find(track_id);
    if (pattern_it == patterns.end()) continue;
    pattern_it->second.deleteNotesWithValue(note);
  }

  // Same cleanup across this track's own clips (ArrangementOps.h) - a
  // clip's leaf Pattern is step data too, just not living directly in any
  // one section's own background.
  for (auto & clip : song.getClips(track_id)) {
    clip.getLeafPattern().deleteNotesWithValue(note);
  }
}

void
PercussionTrack::clearAllLanes(Song & song) {
  for (int note : vector<int>(lane_notes_)) removeLane(note, song); // copy: removeLane mutates lane_notes_
}

vector<int>
PercussionTrack::getHitNotesForRow(const Pattern & pattern, int pattern_row, int context_length) const {
  return getHitNotesAtRow(pattern, pattern.getEffectiveRow(pattern_row, context_length));
}

vector<int>
PercussionTrack::getHitNotesAtRow(const Pattern & pattern, int effective_row) const {
  vector<int> hits;
  auto & notes = pattern.getNotes(effective_row);
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
