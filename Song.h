#ifndef _SONG_H_
#define _SONG_H_

#include "Track.h"
#include "Pattern.h"
#include "InstrumentSet.h"

#include <memory>
#include <vector>

class SongState;
class InstrumentProvider;

class Song : public Track {
 public:
  Song(Tuning _tuning = Tuning::TET12, short _key = -1, float _randomization_factor = 0.01f) : tuning(_tuning), key_note_number(_key), randomization_factor(_randomization_factor) { }

  Tuning getTuning() const { return tuning; }
  void setTuning(Tuning _tuning) { tuning = _tuning; }
  
  short getKey() const { return key_note_number; }
  void setKey(int key) { key_note_number = key; }
  
  float getRandomizationFactor() const { return randomization_factor; }
  void setRandomizationFactor(float f) { randomization_factor = f; }
  const std::string & getName() const { return name; }
  
  short getTempo() const { return bpm; }
  void setTempo(short _bpm) { bpm = _bpm; }
  
  const std::vector<Pattern> & getPatterns() const { return patterns; }
  const Pattern & getPattern(size_t i) const { return i < patterns.size() ? patterns[i] : empty_pattern; }
  Pattern & getPattern(size_t i) { return i < patterns.size() ? patterns[i] : empty_pattern; }

  Pattern & addPattern(const Pattern & pattern) {
    incVersion();
    patterns.push_back(pattern);
    return patterns.back();
  }

  Pattern & addPattern(size_t rows, Tuning tuning = Tuning::INHERIT, int key = -1) { return addPattern(Pattern(rows, tuning, key)); }
    
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

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  SampleData render(size_t frames, SongState & state);

private:
  Tuning tuning;
  short key_note_number;
  float randomization_factor;
  std::string name;
  int bpm = 90;

  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Pattern> patterns;
  Pattern empty_pattern;
  int version = 1;
};

#endif

