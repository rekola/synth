#ifndef _PATTERN_H_
#define _PATTERN_H_

#include "SongObject.h"
#include "Note.h"
#include "Command.h"
#include "VisibleTrackInfo.h"

#include <string>
#include <vector>
#include <unordered_map>

// One track's own note/command content for one Scene (Scene.h) - what used
// to be one track's slice of the old, all-tracks-at-once class also named
// Pattern, now a standalone object in its own right rather than
// interleaved with every other track's content in one shared
// row->track_id->notes map. No track_id anywhere in here: which track this
// belongs to is whichever container holds it - Scene::patterns_by_track_id_'s
// key for a scene's own inline Pattern, or a Clip's own patterns_by_track_
// key (Clip.h) for a reusable one - not this class's own concern.
class Pattern : public SongObject {
 public:
  // A Pattern has a length. `length_ == 0` (the default) isn't a "looping
  // is off" flag - it means this particular Pattern was never given a
  // length of its own, so it takes on whatever length the caller-supplied
  // `context_length` provides (in practice, the containing scene's own
  // effective length - Song::getEffectiveSceneLength() - the same
  // implicit default every Pattern already has). Giving it a shorter
  // length explicitly is the only thing that changes: reads/writes past
  // it wrap, which is simply what "shorter than the span it's played
  // across" already means - not a separate feature to turn on.
  //
  // Every row-taking accessor below takes a raw row, not this effective
  // one - a caller resolves it once via getEffectiveRow(row,
  // song.getEffectiveSceneLength(scene)) before reading or writing,
  // rather than this class remapping internally. Resolved fresh at each
  // call rather than baked in at construction time so a Pattern that was
  // never given its own length keeps tracking its scene's own length live
  // if that ever changes (Scene::setLengthBars()) - snapshotting it in at
  // creation would silently desync the moment the scene's own length
  // changed afterward. No divisibility requirement between length_ and
  // context_length - row % length_ is well-defined either way; a 5-row
  // pattern inside a 64-row context just plays some full repeats plus one
  // partial one, cut off wherever the context ends, the same as stopping
  // a real loop mid-cycle.
  int getLength() const { return length_; }
  void setLength(int length) { length_ = length; }

  int getEffectiveRow(int row, int context_length) const {
    auto len = length_ > 0 ? length_ : context_length;
    return len > 0 ? row % len : row;
  }

  // Overrides SongObject::loadParameters()/storeParameters() but
  // deliberately doesn't call the base version, which handles id_/name_:
  // a Pattern has no name or id of its own, just its length
  // (<pattern length="...">). Called from Song.cpp's shared reader/writer
  // helpers (parsePatternContent()/storePatternContent()), used by both
  // the per-scene and per-clip paths.
  void loadParameters(const ParameterSource & input) override {
    setLength(input.get<int>("length", 0));
  }

  void storeParameters(ParameterSource & output) const override {
    output.set("length", getLength(), 0);
  }

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

  // Removes every defined note across every row whose getValue() ==
  // `value`, regardless of column - DrumMachineTrack::removeLane()'s own
  // per-Pattern half: a removed lane's GM number no longer identifies
  // anything, so any note referencing it (on or off - an off marker still
  // carries the same value(), see Note::isOff()'s own comment) is purged
  // wherever it appears, not just at one row/column. Aftertouch entries
  // (value() == -1) are never matched - they aren't lane-specific to
  // begin with.
  void deleteNotesWithValue(int value) {
    for (auto it = notes_.begin(); it != notes_.end(); ) {
      auto & nv = it->second;
      for (auto & n : nv) {
	if (n.isDefined() && n.getValue() == value) n.clear();
      }
      while (!nv.empty() && !nv.back().isDefined()) nv.pop_back();
      if (nv.empty()) it = notes_.erase(it);
      else ++it;
    }
  }

  // Shifts both the notes and the effect Command together - a row's
  // command is as much "part of that row" as its notes are, so a row
  // shift that moved one but left the other in place would silently
  // desync a command from the note it was meant to modify.
  void insertRow(int row, int num_rows) {
    for (int i = num_rows - 1; i > row; i--) {
      setNotes(i, getNotes(i - 1));
      shiftCommand(i, i - 1);
    }
    clearNotes(row);
    clearCommand(row);
  }

  // The inverse shift: removes `row` itself, pulling every row below it up
  // by one. Nothing exists below the pattern's last row to pull up into
  // it, so that one is cleared instead - the same "somewhere has to end up
  // empty" trade insertRow() makes at the row it displaces from, mirrored.
  void deleteRow(int row, int num_rows) {
    for (int i = row; i < num_rows - 1; i++) {
      setNotes(i, getNotes(i + 1));
      shiftCommand(i, i + 1);
    }
    clearNotes(num_rows - 1);
    clearCommand(num_rows - 1);
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

  void clearCommand(int row) {
    commands_.erase(static_cast<unsigned short>(row));
  }

  const Command & getCommand(int row) const {
    auto it = commands_.find(static_cast<unsigned short>(row));
    return it != commands_.end() ? it->second : empty_command;
  }

  // The raw map, letting a caller list every row that has a command
  // without checking each row individually. Song.cpp's XML writer uses
  // this to decide which rows to write a <command> element for.
  const std::unordered_map<unsigned short, Command> & getCommands() const { return commands_; }

  // The raw sparse row->note-columns map - notes_ itself, letting a caller
  // list every defined row without an outside bound to loop against. A
  // scene's own inline Pattern is always written by looping row 0..the
  // song's own pattern length (its natural bound - see Song.cpp's writer);
  // a clip's own leaf Pattern has no such context, so Song.cpp's own clip
  // writer uses this instead.
  const std::unordered_map<unsigned short, std::vector<Note> > & getNotesByRow() const { return notes_; }

  // Scans every row this Pattern actually has content on, tracking the
  // widest note-column count seen - Scene::getTrackInformation() calls
  // this once per track rather than reconstructing the old flat
  // row->track_id->notes map just to re-derive the same thing.
  void updateSubtrackInfo(VisibleTrackInfo & info) const {
    for (auto & [ row, notes ] : notes_) {
      info.updateNumSubtracks(static_cast<int>(notes.size()));
    }
  }

  // No notes and no commands anywhere - both maps are already kept
  // pruned back to empty as content is cleared (setNotes()/setNote()/
  // deleteNote() drop a row entirely once nothing defined is left in it,
  // clearCommand() erases rather than storing an undefined value), so
  // this is a real "has this ever had content that's still here" check,
  // not just "was this row touched."
  bool isEmpty() const { return notes_.empty() && commands_.empty(); }

  // True iff at least one note here is a genuine, sound-producing note-on
  // (Note::isDefined() && !isOff() && !isAftertouch(), which already
  // covers percussion note-ons correctly too). A Pattern can be non-empty
  // (isEmpty() above) purely from note-offs, aftertouch, or Command data -
  // this tells that case apart from one that actually produces sound.
  bool hasSoundingNote() const {
    for (auto & [ row, columns ] : notes_) {
      for (auto & n : columns) {
        if (n.isDefined() && !n.isOff() && !n.isAftertouch()) return true;
      }
    }
    return false;
  }

private:
  // insertRow()/deleteRow()'s own command-shifting step: copies dst_row's
  // command from src_row, or clears dst_row if src_row had none - mirrors
  // setNotes(getNotes(...))'s own "erase rather than store an explicitly
  // empty value" convention (commands_ is sparse the same way notes_ is;
  // storing every shifted-in "----" explicitly would defeat that and
  // falsely tell getCommands() every such row has a real command).
  void shiftCommand(int dst_row, int src_row) {
    auto & cmd = getCommand(src_row);
    if (cmd.isDefined()) setCommand(dst_row, cmd);
    else clearCommand(dst_row);
  }

  // sparse note matrix: row -> note_column
  std::unordered_map<unsigned short, std::vector<Note> > notes_;
  std::unordered_map<unsigned short, Command> commands_;

  // 0 = not given its own length - see getEffectiveRow()'s own comment.
  int length_ = 0;
  bool loop_ = true;

  std::string name_;

  static inline Note empty_note;
  static inline std::vector<Note> empty_notes;
  static inline Command empty_command;
};

#endif
