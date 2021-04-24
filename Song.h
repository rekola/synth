#ifndef _SONG_H_
#define _SONG_H_

#include "Pattern.h"
// #include "Sequence.h"
#include "Instrument.h"

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

  const std::vector<std::unique_ptr<Instrument> > & getInstruments() const { return instruments; }
  Instrument & getInstrument(size_t i) { return *(instruments[i]); }
  void addInstrument(std::unique_ptr<Instrument> i) {
    instruments.push_back(std::move(i));
    incVersion();
  }

  void incVersion() { version++; }
  int getVersion() const { return version; }
  
  float mastervol = 1.0;
  int bpm = 60;
  
private:
  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Pattern> patterns;
  Pattern empty_pattern;
  int version = 1;
};

#endif
