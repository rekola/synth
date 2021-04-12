#ifndef _SONG_H_
#define _SONG_H_

#include "Track.h"
#include "Channel.h"
#include "Instrument.h"

#include <memory>
#include <vector>

class Song {
 public:
  Song() { }

  const Channel & getPattern(size_t i) const { return i < patt.size() ? patt[i] : empty_pattern; }
  const std::vector<Track> & getTracks() const { return trk; }

  std::vector<Track> trk;
  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Channel> patt;
  
  float mastervol = 1.0;
  float gvol = 1.0; // (or 1.0 / trkcnt)
  int bpm = 60;
  
private:
  // Track empty_track;
  Channel empty_pattern;
};

#endif
