#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "Note.h"

#define PATTLEN 32

#include <string>
#include <unordered_map>

class Pattern {
 public:
  explicit Pattern() { }
  
  size_t getNumRows() const { return PATTLEN; }

  void setNote(size_t track, size_t row, Note note) {
    notes[track][row] = note;
  }

  const Note & getNote(size_t track, size_t row) const {
    auto it = notes.find(track);
    if (it != notes.end()) {
      auto it2 = it->second.find(row);
      if (it2 != it->second.end()) {
	return it2->second;
      }
    }
    return empty_note;
  }
  
private:
  std::string name;
  size_t num_rows = PATTLEN;
  std::unordered_map<unsigned short, std::unordered_map<unsigned short, Note> > notes;
  Note empty_note;
};

#endif
