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
    while (note_column >= columns.size()) columns.push_back(Note());
    columns[note_column] = note;
  }

  void pushNote(size_t row, size_t track, Note note) {
    auto & columns = notes[row][track];
    for (size_t i = 0; i < columns.size(); i++) {
      if (!columns[i].isDefined()) {
	columns[i] = note;
	return;
      }
    }
    columns.push_back(note);
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
  
  const std::vector<size_t> getTrackWidths() const {
    std::vector<size_t> r;
    for (auto & d0 : notes) {
      for (auto & d1 : d0.second) {
	size_t track = d1.first;
	size_t w = d1.second.size();
	while (r.size() <= track) r.push_back(0);
	if (w > r[track]) r[track] = w;
      }
    }
    return r;
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
