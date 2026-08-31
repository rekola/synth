#ifndef _DRUMMACHINETRACK_H_
#define _DRUMMACHINETRACK_H_

#include "LeafTrack.h"

#include <string>
#include <unordered_map>
#include <vector>

class Song;
class Pattern;

// A step-sequencer drum track: the kit only - which drums this track can
// play (lane_notes_) - not what triggers when. A lane hit's actual step
// data is an ordinary Pattern, per scene, in Scene::patterns_by_track_id_,
// exactly like any other track's content (a step is just a Note whose
// value is the lane's own GM number and whose row is the step index - see
// getHitNotesForRow() below); Song.cpp gives the kit its own dedicated
// <drumMachineTrack> parse/write path (lanes only, no step data), kept
// separate from the generic per-track-child recursion the same way it
// always was.
//
// Lane order is *always* derived from DrumRankTable::orderLanes() - never
// authored or stored as an independent ordering - so inserting/removing a
// lane can never desync a stored order from the rank table. addLane()/
// removeLane() are the only way to change the lane list, so the lane list
// and whatever step data references it always change together.
class DrumMachineTrack : public LeafTrack {
public:
  DrumMachineTrack() : LeafTrack(TrackType::DRUM_MACHINE) { }

  const char * getElementName() const override { return "drumMachineTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;

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

  // Adds the default rock kit's 8 lanes (kick/snare/toms/hi-hats/crash -
  // see the .cpp for the exact note list), on top of whatever lanes
  // already exist (addLane() is itself a safe no-op for a note that
  // already has one, so this only fills in what's missing). Called from
  // two places that both want a fresh track to start pre-populated
  // rather than silent: the "add-drum-machine-track" command, and
  // Song.cpp's loadDrumMachineData() for a hand-authored
  // <drumMachineTrack> with no lanes of its own at all (see that
  // function's own comment) - a file that actually specifies its own
  // (however sparse) lane list is never touched by this.
  void seedDefaultKit();

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
  // song.getPatternLength() for the background) - calling
  // getHitNotesForRow() on an already-wrapped row would risk wrapping it
  // a second time against the wrong one.
  std::vector<int> getHitNotesAtRow(const Pattern & pattern, int effective_row) const;

private:
  std::vector<int> lane_notes_;
};

#endif
