#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "SongObject.h"
#include "Note.h"
#include "Command.h"
#include "VisibleTrackInfo.h"

#include <string>
#include <vector>
#include <unordered_map>

class Pattern : public SongObject {
 public:
  explicit Pattern(int _num_rows = 0) : num_rows(_num_rows) { }

  int getNumRows() const { return num_rows; }

  void setNotes(int row, int track_id, const std::vector<Note> & n) {
    notes[row][track_id] = n;
  }
  
  void setNote(int row, int track_id, int note_column, Note note) {
    auto & columns = notes[row][track_id];
    while (note_column >= columns.size()) columns.push_back(Note());
    columns[note_column] = note;
  }

  void setNoteSwapped(int track_id, size_t row, int note_column, Note note) {
    setNote(row, track_id, note_column, note);
  }

  int pushNote(int row, int track_id, Note note) {
    auto & columns = notes[row][track_id];
    for (int i = 0; i < static_cast<int>(columns.size()); i++) {
      if (!columns[i].isDefined()) {
	columns[i] = note;
	return i;
      }
    }
    auto index = columns.size();
    columns.push_back(note);
    return index;
  }

  void clearNotes(int row, int track_id) {
    auto it = notes.find(row);
    if (it != notes.end()) {
      it->second.erase(track_id);
    }
  }
  
  void deleteNote(int row, int track_id, int column) {
    auto it = notes.find(row);
    if (it != notes.end()) {
      auto it2 = it->second.find(track_id);
      if (it2 != it->second.end()) {
	auto & nv = it2->second;
	if (column < static_cast<int>(nv.size())) {
	  nv[column].clear();
	  while (!nv.empty() && !nv.back().isDefined()) nv.pop_back();
	  if (nv.empty()) it->second.erase(it2);
	}
      }
    }
  }

  void insertRow(int row, int track_id) {
    for (int i = getNumRows() - 1; i > row; i--) {
      setNotes(i, track_id, getNotes(i - 1, track_id));
    }
    clearNotes(row, track_id);
  }

  const Note & getNote(int row, int track_id, int note_column) const {
    auto it = notes.find(row);
    if (it != notes.end()) {
      auto it2 = it->second.find(track_id);
      if (it2 != it->second.end()) {
	auto & columns = it2->second;
	if (note_column < columns.size()) return columns[note_column];	
      }
    }
    return empty_note;
  }

  const std::vector<Note> & getNotes(int row, int track_id) const {
    auto it = notes.find(row);
    if (it != notes.end()) {
      auto it2 = it->second.find(track_id);
      if (it2 != it->second.end()) {
	return it2->second;
      }
    }
    return empty_notes;
  }

  const std::unordered_map<int, std::vector<Note> > & getNotes(int row) const {
    auto it = notes.find(row);
    if (it != notes.end()) {
      return it->second;
    } else {
      return empty_notes2;
    }
  }

  void setCommand(int row, int track_id, Command command) {
    commands[row][track_id] = command;
  }

  const Command & getCommand(int row, int track_id) const {
    auto it = commands.find(row);
    if (it != commands.end()) {
      auto it2 = it->second.find(track_id);
      if (it2 != it->second.end()) {
	return it2->second;
      }
    }
    return empty_command;
  }

  const std::unordered_map<int, Command> & getCommands(int row) const {
    auto it = commands.find(row);
    if (it != commands.end()) {
      return it->second;
    } else {
      return empty_commands;
    }
  }

  void getTrackInformation(std::unordered_map<int, VisibleTrackInfo> & track_info) const {
    for (auto & d0 : notes) {
      // auto row = d0.first;
      for (auto & d1 : d0.second) {
	auto track_id = d1.first;
	auto num_notes = d1.second.size();
	auto & info = track_info[track_id];
	info.updateNumSubtracks(num_notes);
      }
    }
  }

  void setAnnotation(int row, std::string a) {
    annotations[row] = std::move(a);
  }
  
  const std::string & getAnnotation(int row) const {
    auto it = annotations.find(row);
    if (it != annotations.end()) return it->second;
    else return empty_string;
  }

  const std::unordered_map<unsigned short, std::string> & getAnnotations() const { return annotations; }

  void transposeUp() {
    for (auto & d0 : notes) {
      for (auto & d1 : d0.second) {
	for (auto & note : d1.second) {
	  note.transposeUp();
	}
      }
    }    
  }

  void transposeDown() {
    for (auto & d0 : notes) {
      for (auto & d1 : d0.second) {
	for (auto & note : d1.second) {
	  note.transposeDown();
	}
      }
    }    
  }

  void loadParameters(const ParameterSource & input) override {
    SongObject::loadParameters(input);
    num_rows = input.getInt("rows");	
  }
  
  void storeParameters(ParameterSource & output) const override {
    SongObject::storeParameters(output);
    output.set("rows", getNumRows());    
  }

private:
  int num_rows;

  // sparse note matrix: row, track, note_column
  std::unordered_map<unsigned short, std::unordered_map<int, std::vector<Note> > > notes;
  std::unordered_map<unsigned short, std::unordered_map<int, Command> > commands;
  std::unordered_map<unsigned short, std::string> annotations;

  Note empty_note;
  std::vector<Note> empty_notes;
  std::unordered_map<int, std::vector<Note> > empty_notes2;
  std::string empty_string;
  std::unordered_map<int, Command> empty_commands;
  Command empty_command;
};

#endif
