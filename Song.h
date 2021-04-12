#ifndef _SONG_H_
#define _SONG_H_

#include "Track.h"
#include "Sequence.h"
#include "Instrument.h"

#include <memory>
#include <vector>

#define PATTLEN 32

class Song {
 public:
  Song() { }

  const std::vector<Track> & getTracks() const { return tracks; }
  void addTrack(const Track & track) { tracks.push_back(track); }

  const std::vector<Sequence> & getSequences() const { return sequences; }
  void addSequence(const Sequence & seq) { sequences.push_back(seq); }
  const Sequence & getSequence(size_t i) const { return i < sequences.size() ? sequences[i] : empty_sequence; }

  const std::vector<std::unique_ptr<Instrument> > & getInstruments() const { return instruments; }
  Instrument & getInstrument(size_t i) { return *(instruments[i]); }
  void addInstrument(std::unique_ptr<Instrument> i) { instruments.push_back(std::move(i)); }
  
  float mastervol = 1.0;
  float gvol = 1.0; // (or 1.0 / trkcnt)
  int bpm = 60;
  
private:
  std::vector<std::unique_ptr<Instrument> > instruments;
  // Track empty_track;
  std::vector<Track> tracks;
  std::vector<Sequence> sequences;
  Sequence empty_sequence;
};

#endif
