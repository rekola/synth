#ifndef _PATTERNVIEW_H_
#define _PATTERNVIEW_H_

#include "Command.h"
#include "Note.h"
#include "Pattern.h"
#include "ScoreSchema.h"
#include "VisibleTrackInfo.h"
#include "../doc/Document.h"

#include <map>
#include <vector>

// A track's (or a clip's) notes and commands, as they are in the document:
// a short-lived handle on a "pattern" node with the same operations as the
// value class Pattern, which it replaces for anything that edits or displays
// the song. It holds no content of its own, so it cannot go stale or be
// copied apart from the document; a copy is another handle on the same
// notes. Use it and drop it - a delete can detach the node under it.
//
// Reads return values, not references into storage. A mutation made outside
// a Song::Edit is its own edit (Document::ImplicitScope).
class PatternView {
 public:
  PatternView() = default;
  PatternView(doc::Document * document, doc::NodeId node) : doc_(document), node_(node) { }

  bool valid() const { return doc_ && doc_->get(node_); }
  doc::NodeId node() const { return node_; }
  explicit operator bool() const { return valid(); }

  // 0 = not given its own length (Pattern::getEffectiveRow()).
  int getLength() const;
  void setLength(int length);
  int getEffectiveRow(int row, int context_length) const {
    auto len = getLength() > 0 ? getLength() : context_length;
    return len > 0 ? row % len : row;
  }

  void setNotes(int row, const std::vector<Note> & notes);
  void setNote(int row, int note_column, Note note);
  int pushNote(int row, Note note);
  void clearNotes(int row);
  void deleteNote(int row, int column);
  void deleteNotesWithValue(int value);
  void insertRow(int row, int num_rows);
  void deleteRow(int row, int num_rows);

  Note getNote(int row, int note_column) const;
  std::vector<Note> getNotes(int row) const;
  std::map<int, std::vector<Note> > getNotesByRow() const;

  void setCommand(int row, int command_column, Command command);
  void setCommand(int row, Command command) { setCommand(row, 0, command); }
  int pushCommand(int row, Command command);
  void clearCommands(int row);
  void clearCommand(int row) { deleteCommand(row, 0); }
  void deleteCommand(int row, int command_column);
  Command getCommand(int row, int command_column) const;
  Command getCommand(int row) const { return getCommand(row, 0); }
  std::vector<Command> getCommandsAt(int row) const;
  std::map<int, std::vector<Command> > getCommandsByRow() const;

  int getContentEnd() const;
  void updateSubtrackInfo(VisibleTrackInfo & info) const;
  bool isEmpty() const;
  bool hasSoundingNote() const;

  // Replaces everything with `pattern`'s content, changing only what differs.
  void assign(const Pattern & pattern);
  // The plain value, for the published copy and the clipboard.
  Pattern toPattern() const;

  // A "pattern" node with no content, not yet attached.
  static doc::NodeId create(doc::Document & document);

 private:
  const std::vector<doc::NodeId> & ids(const char * slot) const;
  // The first child of `slot` at or after (row, column).
  size_t lowerBound(const char * slot, int row, int column) const;
  // The children of `slot` on `row`, as [first, last) indices.
  std::pair<size_t, size_t> rowRange(const char * slot, int row) const;
  doc::NodeId findAt(const char * slot, int row, int column) const;
  int rowOf(doc::NodeId child) const;
  int columnOf(doc::NodeId child) const;
  Note noteOf(doc::NodeId child) const;
  Command commandOf(doc::NodeId child) const;
  void shiftRows(const char * slot, int first_row, int last_row, int delta);

  doc::Document * doc_ = nullptr;
  doc::NodeId node_ = doc::kNoNode;
};

#endif
