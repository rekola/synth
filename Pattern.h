#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "Track.h"

#define PATTLEN 32

#include <vector>

class Pattern {
 public:
  explicit Pattern() { }

  std::vector<Track> & getTracks() { return tracks; }
  const std::vector<Track> & getTracks() const { return tracks; }
  const Track & getTrack(size_t i) const { return i < tracks.size() ? tracks[i] : empty_track; }
  Track & getTrack(size_t i) { return i < tracks.size() ? tracks[i] : empty_track; }
  Track & addTrack(const Track & s) { tracks.push_back(s); return tracks.back(); }
  Track & addTrack() { return addTrack(Track()); }
  bool empty() const { return tracks.empty(); }

  size_t getTrackCount() const { return tracks.size(); }
  size_t getRowCount() const { return PATTLEN; }

private:
  std::vector<Track> tracks;
  Track empty_track;
};

#endif
