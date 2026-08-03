#ifndef _DRUMMACHINETRACK_H_
#define _DRUMMACHINETRACK_H_

#include "InstrumentTrack.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// A step-sequencer drum track: an N-step loop (loop_length_, fixed at 8 for
// the MVP - see plans/drum-machine.md) that repeats across the whole
// pattern, one lane per GM percussion note. The sequence lives entirely on
// this track, never in Pattern row data - Song.cpp gives it a dedicated
// <drumMachine> parse/write path, kept separate from the generic per-
// track-child recursion (a <drumMachine> element is data, not a nested
// Track).
//
// Lane order is *always* derived from DrumRankTable::orderLanes() - never
// authored or stored as an independent ordering - so inserting/removing a
// lane can never desync a stored order from the rank table. Step data is
// keyed by GM note number, not lane index, for the same reason: reordering
// lanes must never remap which steps belong to which drum. addLane()/
// removeLane() are the only way to change the lane list, so the lane list
// and that lane's step data always change together (see their own
// comments) - no separate commit step to get out of sync.
class DrumMachineTrack : public InstrumentTrack {
public:
  DrumMachineTrack() : InstrumentTrack(TrackType::DRUM_MACHINE) { }

  const char * getElementName() const override { return "drumMachineTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;

  int getLoopLength() const { return loop_length_; }
  void setLoopLength(int n) { loop_length_ = n; }

  // Not read/written by anything yet - carried through save/load only, so
  // the later reusable-named-sequence roadmap item (plans/drum-machine.md
  // Appendix C) can reference a sequence by id without a format break.
  const std::string & getSequenceId() const { return sequence_id_; }
  void setSequenceId(std::string id) { sequence_id_ = std::move(id); }

  // Lanes, bottom-to-top, always in DrumRankTable order.
  const std::vector<int> & getLaneNotes() const { return lane_notes_; }

  bool hasLane(int note) const;

  // The step-grid surface (Phase 5, LaunchpadManager.cpp) has exactly 8
  // rows - one per lane - so a kit can never display/edit more than 8
  // lanes at once no matter how many the picker lets you pick. Same "hard
  // ceiling, not a stepping stone" shape as the ambisonic order cap
  // elsewhere in this codebase (see AmbisonicEncoding.h).
  static constexpr int kMaxLanes = 8;

  // Adds a lane for `note` (no-op if it already has one, or if the track
  // is already at kMaxLanes), seeded all-rest, and re-derives lane order
  // immediately.
  void addLane(int note);

  // Removes `note`'s lane and silently deletes its step data - no
  // confirmation, no undo (see plans/drum-machine.md's own risk note on
  // this). No-op if `note` has no lane.
  void removeLane(int note);

  // Removes every lane at once.
  void clearAllLanes();

  // Adds the default rock kit's 8 lanes (kick/snare/toms/hi-hats/crash -
  // see the .cpp for the exact note list), all-rest, on top of whatever
  // lanes already exist (addLane() is a no-op for a note that already has
  // one, so this only fills in what's missing). Called from two places
  // that both want a fresh track to start pre-populated rather than
  // silent: the "add-drum-machine-track" command, and Song.cpp's
  // loadDrumMachineData() for a hand-authored `<drumMachineTrack>` with no
  // `<drumMachine>` child at all (see that function's own comment) - a
  // file that actually specifies its own sequence data, however sparse,
  // is never touched by this, only a track with zero explicit data.
  void seedDefaultKit();

  // Bit i (0 = first step) of `note`'s lane, or 0 if `note` has no lane.
  // Bits at or past getLoopLength() are unused.
  uint8_t getSteps(int note) const;
  void setSteps(int note, uint8_t steps);
  void setStep(int note, int step, bool hit);

  // Which lane notes are hit at pattern-relative row `pattern_row`
  // (source_row = pattern_row % getLoopLength()) - a pure function of
  // this track's own data (loop length + step masks) and the row index
  // alone, with no persisted "which step am I on" counter, so it gives
  // identical results whether called in playback order or after an
  // arbitrary seek (see plans/drum-machine.md's seek-correctness
  // invariant). Pattern-boundary truncation/reset is the caller's for
  // free: SongState::render() only ever calls this with a `pattern_row`
  // already made pattern-relative by Song::getRelativePosition(), which
  // is always < that pattern's own row count and always restarts at 0
  // for the next pattern - this function needs no boundary awareness of
  // its own.
  std::vector<int> getHitNotesForRow(int pattern_row) const;

private:
  int loop_length_ = 8;
  std::string sequence_id_;
  std::vector<int> lane_notes_;
  std::unordered_map<int, uint8_t> steps_;
};

#endif
