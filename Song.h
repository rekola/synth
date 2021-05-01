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
  Song(Tuning _tuning = Tuning::TET12, short _key = -1, float _randomization_factor = 0.01f) : tuning(_tuning), key_note_number(_key), randomization_factor(_randomization_factor) { }

  Tuning getTuning() const { return tuning; }
  short getKey() const { return key_note_number; }
  float getRandomizationFactor() const { return randomization_factor; }
  const std::string & getName() const { return name; }
  
  short getTempo() const { return bpm; }
  void setTempo(short _bpm) { bpm = _bpm; }

  float getMasterVolume() const { return master_volume; }
  void setMasterVolume(float v) { master_volume = v; }
  
  const std::vector<Pattern> & getPatterns() const { return patterns; }
  const Pattern & getPattern(size_t i) const { return i < patterns.size() ? patterns[i] : empty_pattern; }
  Pattern & getPattern(size_t i) { return i < patterns.size() ? patterns[i] : empty_pattern; }

  void addPattern(const Pattern & pattern) {
    patterns.push_back(pattern);
    incVersion();
  }

  Track & getMasterTrack() { return master_track; }
  const Track & getMasterTrack() const { return master_track; }
    
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

private:
  Tuning tuning;
  short key_note_number;
  float randomization_factor;
  std::string name;
  int bpm = 90;
  float master_volume = 1.0;

  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Pattern> patterns;
  Pattern empty_pattern;
  int version = 1;

  Track master_track;
};

#endif
