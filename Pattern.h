#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "Note.h"

#define DEFAULT_PATTERN_LENGTH 32

#include <string>
#include <vector>
#include <unordered_map>

class Pattern {
 public:
  explicit Pattern(size_t _num_rows = DEFAULT_PATTERN_LENGTH) : num_rows(_num_rows) { }
  
  size_t getNumRows() const { return num_rows; }

  void setNote(size_t row, size_t track, size_t note_column, Note note) {
    auto & columns = notes[row][track];
    while (note_column >= columns.size()) columns.push_back(note);
    columns[note_column] = note;
  }

  const Note & getNote(size_t row, size_t track, size_t note_column) const {
    auto it = notes.find(row);
    if (it != notes.end()) {
      auto it2 = it->second.find(track);
      if (it2 != it->second.end()) {
	auto & columns = it2->second;
	if (note_column < columns.size()) return columns[note_column];	
      }
    }
    return empty_note;
  }

  const std::vector<Note> & getNotes(size_t row, size_t track) const {
    auto it = notes.find(row);
    if (it != notes.end()) {
      auto it2 = it->second.find(track);
      if (it2 != it->second.end()) {
	return it2->second;
      }
    }
    return empty_notes;
  }

private:
  std::string name;
  size_t num_rows;
  // sparse note matrix: row, track, note_column
  std::unordered_map<unsigned short, std::unordered_map<unsigned short, std::vector<Note> > > notes;
  Note empty_note;
  std::vector<Note> empty_notes;
};

#endif
