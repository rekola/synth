#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "Note.h"
#include "Tuning.h"

#define DEFAULT_PATTERN_LENGTH 32

#include <string>
#include <vector>
#include <unordered_map>

class Pattern {
 public:
  explicit Pattern(size_t _num_rows = DEFAULT_PATTERN_LENGTH) : num_rows(_num_rows) { }

  Tuning getTuning() const { return tuning; }
  size_t getNumRows() const { return num_rows; }

  void setNote(size_t track, size_t row, size_t note_column, Note note) {
    auto & columns = notes[track][row];
    while (note_column >= columns.size()) columns.push_back(Note());
    columns[note_column] = note;
  }

  size_t pushNote(size_t track, size_t row, Note note) {
    auto & columns = notes[track][row];
    for (size_t i = 0; i < columns.size(); i++) {
      if (!columns[i].isDefined()) {
	columns[i] = note;
	return i;
      }
    }
    size_t index = columns.size();
    columns.push_back(note);
    return index;
  }

  const Note & getNote(size_t track, size_t row, size_t note_column) const {
    auto it = notes.find(track);
    if (it != notes.end()) {
      auto it2 = it->second.find(row);
      if (it2 != it->second.end()) {
	auto & columns = it2->second;
	if (note_column < columns.size()) return columns[note_column];	
      }
    }
    return empty_note;
  }

  const std::vector<Note> & getNotes(size_t track, size_t row) const {
    auto it = notes.find(track);
    if (it != notes.end()) {
      auto it2 = it->second.find(row);
      if (it2 != it->second.end()) {
	return it2->second;
      }
    }
    return empty_notes;
  }
  
  const std::vector<size_t> getTrackWidths() const {
    std::vector<size_t> r;
    for (auto & d0 : notes) {
      auto track = d0.first;
      for (auto & d1 : d0.second) {
	// auto row = d1.first;
	size_t w = d1.second.size();
	while (r.size() <= track) r.push_back(0);
	if (w > r[track]) r[track] = w;
      }
    }
    return r;
  }

  void setAnnotation(size_t row, std::string s) {
    annotations[row] = s;
  }
  
  const std::string & getAnnotation(size_t row) const {
    auto it = annotations.find(row);
    if (it != annotations.end()) return it->second;
    else return empty_string;
  }

private:
  short key_note_number = 0;
  Tuning tuning = Tuning::INHERIT;
  std::string name;
  size_t num_rows;
  // sparse note matrix: row, track, note_column
  std::unordered_map<unsigned short, std::unordered_map<unsigned short, std::vector<Note> > > notes;
  Note empty_note;
  std::vector<Note> empty_notes;
  std::unordered_map<unsigned short, std::string> annotations;
  std::string empty_string;
};

#endif
