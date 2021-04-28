#ifndef _SONG_H_
#define _SONG_H_

#include "Track.h"
#include "Pattern.h"
// #include "Sequence.h"
#include "InstrumentSet.h"

#include <memory>
#include <vector>

class Song {
 public:
  Song() { }

  const std::vector<Pattern> & getPatterns() const { return patterns; }
  const Pattern & getPattern(size_t i) const { return i < patterns.size() ? patterns[i] : empty_pattern; }
  Pattern & getPattern(size_t i) { return i < patterns.size() ? patterns[i] : empty_pattern; }

  void addPattern(const Pattern & pattern) {
    patterns.push_back(pattern);
    incVersion();
  }

  std::vector<Track> & getTracks() { return tracks; }
  const std::vector<Track> & getTracks() const { return tracks; }
  const Track & getTrack(size_t i) const { return i < tracks.size() ? tracks[i] : empty_track; }
  Track & getTrack(size_t i) { return i < tracks.size() ? tracks[i] : empty_track; }
  Track & addTrack(const Track & s) { tracks.push_back(s); return tracks.back(); }
  Track & addTrack() { return addTrack(Track()); }

  const std::vector<std::unique_ptr<Instrument> > & getInstruments() const { return instruments; }
  Instrument & getInstrument(size_t i) { return *(instruments[i]); }
  void addInstrument(std::unique_ptr<Instrument> i) {
    instruments.push_back(std::move(i));
    incVersion();
  }

  void addInstruments(InstrumentSet & is) {
    auto v = is.createAll();
    for (auto & instrument : v) addInstrument(std::move(instrument));
  }

  void incVersion() { version++; }
  int getVersion() const { return version; }

  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & track : tracks) n += track.getVoiceCount();
    return n;
  }

  size_t getAllocatedVoiceCount() const {
    size_t n = 0;
    for (auto & track : tracks) n += track.getAllocatedVoiceCount();
    return n;
  }

  float mastervol = 1.0;
  int bpm = 60;

private:
  short key_note_number = 0;
  Tuning tuning = Tuning::INHERIT;

  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Pattern> patterns;
  Pattern empty_pattern;
  int version = 1;

  std::vector<Track> tracks;
  Track empty_track;  
};

#endif
