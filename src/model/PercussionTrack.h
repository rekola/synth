#ifndef _PERCUSSIONTRACK_H_
#define _PERCUSSIONTRACK_H_

#include "LeafTrack.h"

#include <string>
#include <unordered_map>
#include <vector>

class Song;
class Pattern;

// Plays through the pool's one drum kit (InstrumentPool::
// getDefaultKitInstrument(), via PercussionTrack.cpp's own local
// PercussionTrackState), not a per-track instrument-pool pick - no
// instrument_id_ of its own.
//
// With no lanes at all, it's an ordinary percussion track: free note entry
// through the free-drumming percussion pad layout (a fixed family/color
// arrangement of GM sounds, not an isomorphic pitched grid - percussion
// has no interval structure for one to preserve), note columns for
// multiple simultaneous hits like any other LeafTrack (not a pitched
// chord - a percussion "note" is just which drum, never a harmony). Once
// it has at least one lane
// (lane_notes_ - which drums this track can play, not what triggers when),
// its grid becomes a step sequencer instead - a lane hit's actual step data
// is an ordinary Pattern, per scene, in Scene::patterns_by_track_id_,
// exactly like any other track's content (a step is just a Note whose
// value is the lane's own GM number and whose row is the step index - see
// getHitNotesForRow() below). isStepSequenced() is the single predicate
// that tells the two states apart - Song.cpp gives lanes their own
// dedicated <percussionTrack> parse/write path (a <lane> child per lane,
// no step data), kept separate from the generic per-track-child recursion
// the same way it always was.
//
// Lane order is *always* derived from DrumRankTable::orderLanes() - never
// authored or stored as an independent ordering - so inserting/removing a
// lane can never desync a stored order from the rank table. addLane()/
// removeLane() are the only way to change the lane list, so the lane list
// and whatever step data references it always change together.
class PercussionTrack : public LeafTrack {
 public:
  PercussionTrack() : LeafTrack(TrackType::PERCUSSION_CONTROL) { }

  const char * getElementName() const override { return "percussionTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;

  // True once this track has at least one lane - see this class's own
  // header comment for what that changes (step-sequenced grid vs. ordinary
  // percussion note entry).
  bool isStepSequenced() const { return !lane_notes_.empty(); }

  // Lanes, bottom-to-top, always in DrumRankTable order.
  const std::vector<int> & getLaneNotes() const { return lane_notes_; }

  bool hasLane(int note) const;

  // The step-grid surface (LaunchpadManager.cpp) has exactly 8 rows - one
  // per lane - so a kit can never display/edit more than 8 lanes at once
  // no matter how many the picker lets you pick. Same "hard ceiling, not
  // a stepping stone" shape as the ambisonic order cap elsewhere in this
  // codebase (see AmbisonicEncoding.h).
  static constexpr int kMaxLanes = 8;

  // Adds a lane for `note` (no-op if it already has one, or if the track
  // is already at kMaxLanes) and re-derives lane order immediately. A
  // freshly added lane starts with no step data anywhere - there's
  // nothing to seed, since steps live in each scene's own Pattern, not
  // here.
  void addLane(int note);

  // Removes `note`'s lane and deletes every step referencing it - no
  // confirmation, no undo - from *every* scene's own Pattern for this
  // track, not just a single track-global map, since that's where step
  // data lives now. No-op if `note` has no lane.
  void removeLane(int note, Song & song);

  // Removes every lane at once.
  void clearAllLanes(Song & song);

  // A named, curated subset of lanes - e.g. Latin means the conga/bongo/
  // timbale-family notes, not a different sound source (every preset still
  // plays through the same pool kit - see this class's own header
  // comment). Every named kit is exactly kMaxLanes notes, so applying one
  // always fills the grid; NONE is the one exception - it has none at
  // all, the explicit way back to a plain, lane-less track.
  enum class Preset { NONE, ROCK, LATIN, ELECTRONIC };

  // Replaces this track's entire lane list with `preset`'s own notes:
  // clearAllLanes() first (deleting whatever step data the previous lanes
  // had - the same no-confirmation, no-undo risk removeLane() already
  // accepts), then addLane() for each of the preset's notes - picking a
  // kit means "use this kit now", not "add these on top of whatever's
  // already there".
  void applyPreset(Preset preset, Song & song);

  // Which of this track's own lane_notes_ are hit at pattern-relative row
  // `pattern_row` of `pattern` (that scene's own Pattern for this track,
  // or a clip's own leaf Pattern - see ArrangementOps.h - the caller
  // already has it either way) - resolved through Pattern::
  // getEffectiveRow(pattern_row, context_length) first, so a shorter
  // pattern's own loop repeats exactly like any other track's content
  // does. A hit is a defined, sound-producing Note (isDefined() &&
  // !isOff() && !isAftertouch()) at that row whose getValue() equals the
  // lane's own GM number - filtered to lane_notes_ (not just "every note
  // present"), so a note left over from a removed lane, or pasted in from
  // a track with a different kit, stays silently inert rather than firing
  // or erroring. Order matches lane_notes_'s own (DrumRankTable) order,
  // not row/column order. A thin wrapper over getHitNotesAtRow() below,
  // for a caller that hasn't already resolved the row itself.
  std::vector<int> getHitNotesForRow(const Pattern & pattern, int pattern_row, int context_length) const;

  // getHitNotesForRow()'s own notes-scan, given an already-resolved row -
  // for a caller that already has one from ArrangementOps.h's
  // resolveReadTarget() (ReadTarget::effective_row), which is already
  // wrapped against the *correct* context length (a clip's own length, or
  // the containing scene's own effective length for the background) -
  // calling getHitNotesForRow() on an already-wrapped row would risk
  // wrapping it a second time against the wrong one.
  std::vector<int> getHitNotesAtRow(const Pattern & pattern, int effective_row) const;

 private:
  std::vector<int> lane_notes_;
};

#endif
