#ifndef _SONGSTRUCTURE_H_
#define _SONGSTRUCTURE_H_

#include "VisibleTrackInfo.h"

#include <unordered_map>
#include <vector>

class Track;
class Song;

// The single source of truth for which of a Song's tracks are addressable
// (pattern-editor columns, Launchpad columns, NoteCoordinate identity) and
// in what order - built once by walking song.getTracks(), replacing what
// used to be three separate, near-duplicate walks (Song.cpp's own
// collectRootTrackIds(), PatternEditor.cpp's fill_track_info(), and an
// earlier draft of this class that only assigned ordinals). Two things per
// qualifying track, both derived from that one walk:
//
// - A stable ordinal (0, 1, 2, ... in encounter order) - see
//   getOrdinalFor(). Exists because Track::getInternalId() - the only
//   identity a track already carries - is drawn from a counter shared by
//   the whole process, not anything about the song itself, and so isn't
//   reproducible across different processes/builds the way
//   NoteCoordinate-seeded jitter needs its track identity to be. Threaded
//   through Track::createState()/createStateTree() the same way
//   needs_decorrelation is threaded through playNote(), so a track can
//   query its own ordinal (structure.getOrdinalFor(*this)) without needing
//   a back-pointer to the Song it belongs to.
// - A baseline VisibleTrackInfo (column shape - see getBaselineInfo()),
//   generalized to every per-track Effect, not just the four leaf
//   InstrumentTrack-ish TrackTypes fill_track_info() used to check.
//
// A default-constructed instance is empty (every lookup misses) - not a
// usable stand-in for a real one; every caller must construct/supply an
// actual instance rather than relying on this as a silent fallback.
class SongStructure {
 public:
  SongStructure() = default;
  explicit SongStructure(const Song & song);

  // -1 if `internal_id` never qualified for an ordinal (not expected for
  // anything that legitimately asks - see class comment above).
  int getOrdinalFor(int internal_id) const;
  int getOrdinalFor(const Track & track) const;

  // Ordinal-ordered list of every qualifying track's internal id - what
  // Song::getRootTrackIds() returns.
  const std::vector<int> & getOrderedTrackIds() const { return ordered_ids_; }

  // Each qualifying track's baseline column shape (TrackType/own-settings
  // derived - not the dynamic, currently-visible-pattern-content-driven
  // widening PatternEditor layers on top separately). Default-constructed
  // VisibleTrackInfo if `internal_id` never qualified.
  const VisibleTrackInfo & getBaselineInfo(int internal_id) const;

 private:
  void visit(const Track & track);

  std::unordered_map<int, int> ordinal_by_id_;
  std::vector<int> ordered_ids_;
  std::unordered_map<int, VisibleTrackInfo> baseline_info_;
};

#endif
