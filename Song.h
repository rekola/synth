#ifndef _SONG_H_
#define _SONG_H_

#include "Section.h"
#include "Sequence.h"
#include "Instrument.h"

#include <memory>
#include <vector>

#define PATTLEN 32

class Song {
 public:
  Song() { }

  const std::vector<Section> & getSections() const { return sections; }
  const Section & getSection(size_t i) const { return i < sections.size() ? sections[i] : empty_section; }
  void addSection(const Section & section) { sections.push_back(section); }

  const std::vector<std::unique_ptr<Instrument> > & getInstruments() const { return instruments; }
  Instrument & getInstrument(size_t i) { return *(instruments[i]); }
  void addInstrument(std::unique_ptr<Instrument> i) { instruments.push_back(std::move(i)); }
  
  float mastervol = 1.0;
  float gvol = 1.0; // (or 1.0 / trkcnt)
  int bpm = 60;
  
private:
  std::vector<std::unique_ptr<Instrument> > instruments;
  // Section empty_section;
  std::vector<Section> sections;
  std::vector<Sequence> sequences;
  Section empty_section;
};

#endif
