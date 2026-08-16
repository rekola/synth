#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "SongObject.h"
#include "Note.h"
#include "Command.h"
#include "VisibleTrackInfo.h"

#include <vector>
#include <unordered_map>

// One track's own note/command content for one Scene (Scene.h) - what used
// to be one track's slice of the old, all-tracks-at-once class also named
// Pattern, now a standalone object in its own right rather than
// interleaved with every other track's content in one shared
// row->track_id->notes map. No track_id anywhere in here: which track this
// belongs to is Scene's own concern (Scene::patterns_by_track_id_'s key),
// not this class's - and nothing here is reused across more than one Scene
// yet (a future step - not yet built - could let the same Pattern be
// referenced from several Scenes, e.g. a drum pattern reused across every
// verse), so there's no id-based lookup need yet either, just plain
// per-row storage.
class Pattern : public SongObject {
 public:
  // Trims trailing undefined notes and drops the row from the sparse map
  // entirely once nothing defined is left in it - the same cleanup
  // deleteNote() below already does, applied here too since a caller can
  // just as easily hand this an all-undefined vector (e.g. pasting a
  // blank source row over an existing one) or overwrite the one defined
  // column a row had with Note()'s undefined value via setNote().
  void setNotes(int row, const std::vector<Note> & n) {
    auto key = static_cast<unsigned short>(row);
    auto columns = n;
    while (!columns.empty() && !columns.back().isDefined()) columns.pop_back();
    if (columns.empty()) notes_.erase(key);
    else notes_[key] = std::move(columns);
  }

  void setNote(int row, int note_column, Note note) {
    auto key = static_cast<unsigned short>(row);
    auto & columns = notes_[key];
    while (note_column >= static_cast<int>(columns.size())) columns.push_back(Note());
    columns[static_cast<size_t>(note_column)] = note;
    while (!columns.empty() && !columns.back().isDefined()) columns.pop_back();
    if (columns.empty()) notes_.erase(key);
  }

  int pushNote(int row, Note note) {
    auto & columns = notes_[static_cast<unsigned short>(row)];
    for (int i = 0; i < static_cast<int>(columns.size()); i++) {
      if (!columns[static_cast<size_t>(i)].isDefined()) {
	columns[static_cast<size_t>(i)] = note;
	return i;
      }
    }
    auto index = columns.size();
    columns.push_back(note);
    return static_cast<int>(index);
  }

  void clearNotes(int row) {
    notes_.erase(static_cast<unsigned short>(row));
  }

  void deleteNote(int row, int column) {
    auto it = notes_.find(static_cast<unsigned short>(row));
    if (it != notes_.end()) {
      auto & nv = it->second;
      if (column < static_cast<int>(nv.size())) {
	nv[static_cast<size_t>(column)].clear();
	while (!nv.empty() && !nv.back().isDefined()) nv.pop_back();
	if (nv.empty()) notes_.erase(it);
      }
    }
  }

  void insertRow(int row, int num_rows) {
    for (int i = num_rows - 1; i > row; i--) {
      setNotes(i, getNotes(i - 1));
    }
    clearNotes(row);
  }

  const Note & getNote(int row, int note_column) const {
    auto it = notes_.find(static_cast<unsigned short>(row));
    if (it != notes_.end() && note_column < static_cast<int>(it->second.size())) return it->second[static_cast<size_t>(note_column)];
    return empty_note;
  }

  const std::vector<Note> & getNotes(int row) const {
    auto it = notes_.find(static_cast<unsigned short>(row));
    return it != notes_.end() ? it->second : empty_notes;
  }

  void setCommand(int row, Command command) {
    commands_[static_cast<unsigned short>(row)] = command;
  }

  const Command & getCommand(int row) const {
    auto it = commands_.find(static_cast<unsigned short>(row));
    return it != commands_.end() ? it->second : empty_command;
  }

  // The raw map, letting a caller list every row that has a command
  // without checking each row individually. Song.cpp's XML writer uses
  // this to decide which rows to write a <command> element for.
  const std::unordered_map<unsigned short, Command> & getCommands() const { return commands_; }

  // Scans every row this Pattern actually has content on, tracking the
  // widest note-column count seen - Scene::getTrackInformation() calls
  // this once per track rather than reconstructing the old flat
  // row->track_id->notes map just to re-derive the same thing.
  void updateSubtrackInfo(VisibleTrackInfo & info) const {
    for (auto & [ row, notes ] : notes_) {
      info.updateNumSubtracks(static_cast<int>(notes.size()));
    }
  }

private:
  // sparse note matrix: row -> note_column
  std::unordered_map<unsigned short, std::vector<Note> > notes_;
  std::unordered_map<unsigned short, Command> commands_;

  static inline Note empty_note;
  static inline std::vector<Note> empty_notes;
  static inline Command empty_command;
};

#endif
