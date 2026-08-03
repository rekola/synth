#ifndef _DRUMRANKTABLE_H_
#define _DRUMRANKTABLE_H_

#include <vector>

// Canonical drum-machine lane ordering (Appendix B of the drum-machine
// plan, plans/drum-machine.md): membranes first low to high, then woods/
// shakers/scrapers, then metals small to large, then effects - drum-
// notation order, so a picked kick ends up at the bottom of the grid and
// a picked crash near the top. Deliberately not GM note-number order,
// which gets several pairs backwards (e.g. 60 Hi Bongo before 61 Low
// Bongo). Pure data + one sort function, no dependency on Track/Song, so
// it's fully unit-testable ahead of DrumMachineTrack existing.
namespace DrumRankTable {

  // rank[note], lower value sorts to a lower lane (bottom of the grid), or
  // -1 if note isn't a ranked percussion sound. Covers all 56 sounds this
  // engine's Launchpad layout supports (GM 27-82 - see
  // LaunchpadLayout.cpp's PERCUSSION_TABLE), not just the 47-note
  // standard-GM range the plan's Appendix B enumerates: the drum picker
  // reuses that same 56-sound layout, so every pickable note needs a
  // rank. Shaker (82) is grouped with the other hand shakers (69 Cabasa,
  // 70 Maracas); the 8 Roland-GS electronic-kit hits and metronome
  // (27-34) form their own trailing group after Effects, since they're
  // the least "core" sounds and the plan's own Appendix B never
  // addresses them.
  int rankForNote(int note);

  // Sorts a set of picked GM notes into lane order (result[0] = bottom of
  // the grid), ties broken by GM note number as a stable fallback (not
  // expected to matter - no two notes share a rank). This is the entire
  // "software orders them, the user never arranges lanes" implementation.
  std::vector<int> orderLanes(std::vector<int> notes);

}

#endif
