#include "PatternView.h"

#include <algorithm>
#include <set>

using namespace scoreschema;

namespace {

void addProp(std::vector<std::pair<std::string, doc::Value> > & props, const doc::Prop<int> & prop, int value) {
  if (!doc::isDefault(value, prop.def)) props.emplace_back(prop.key, doc::toValue(value));
}

}  // namespace

doc::NodeId
PatternView::create(doc::Document & document) {
  return document.create("pattern");
}

const std::vector<doc::NodeId> &
PatternView::ids(const char * slot) const {
  static const std::vector<doc::NodeId> kNone;
  auto n = doc_->get(node_);
  auto list = n ? n->children(slot) : nullptr;
  return list ? *list : kNone;
}

int
PatternView::rowOf(doc::NodeId child) const {
  return doc::get(*doc_, child, kNoteRow);
}

int
PatternView::columnOf(doc::NodeId child) const {
  return doc::get(*doc_, child, kNoteColumn);
}

Note
PatternView::noteOf(doc::NodeId child) const {
  return Note(doc::get(*doc_, child, kNoteValue), static_cast<short>(doc::get(*doc_, child, kNoteVelocity)), static_cast<short>(doc::get(*doc_, child, kNoteDelay)));
}

Command
PatternView::commandOf(doc::NodeId child) const {
  Command command;
  command.setData(doc::getRef(*doc_, child, kCommandData));
  return command;
}

size_t
PatternView::lowerBound(const char * slot, int row, int column) const {
  auto & list = ids(slot);
  size_t lo = 0, hi = list.size();
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    auto r = rowOf(list[mid]);
    if (r < row || (r == row && columnOf(list[mid]) < column)) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

std::pair<size_t, size_t>
PatternView::rowRange(const char * slot, int row) const {
  auto first = lowerBound(slot, row, 0);
  auto & list = ids(slot);
  auto last = first;
  while (last < list.size() && rowOf(list[last]) == row) last++;
  return { first, last };
}

doc::NodeId
PatternView::findAt(const char * slot, int row, int column) const {
  auto index = lowerBound(slot, row, column);
  auto & list = ids(slot);
  return index < list.size() && rowOf(list[index]) == row && columnOf(list[index]) == column ? list[index] : doc::kNoNode;
}

int
PatternView::getLength() const {
  return doc::get(*doc_, node_, kPatternLength);
}

void
PatternView::setLength(int length) {
  doc::set(*doc_, node_, kPatternLength, length);
}

void
PatternView::setNote(int row, int note_column, Note note) {
  if (note_column < 0) return;
  auto index = lowerBound(kNotesSlot, row, note_column);
  auto & list = ids(kNotesSlot);
  bool exists = index < list.size() && rowOf(list[index]) == row && columnOf(list[index]) == note_column;
  if (!note.isDefined()) {
    if (exists) doc_->removeChild(node_, kNotesSlot, index);
    return;
  }
  if (exists) {
    auto node = list[index];
    doc::set(*doc_, node, kNoteValue, note.getValue());
    doc::set(*doc_, node, kNoteVelocity, static_cast<int>(note.getVelocity()));
    doc::set(*doc_, node, kNoteDelay, static_cast<int>(note.getDelay()));
    return;
  }
  std::vector<std::pair<std::string, doc::Value> > props;
  addProp(props, kNoteRow, row);
  addProp(props, kNoteColumn, note_column);
  addProp(props, kNoteValue, note.getValue());
  addProp(props, kNoteVelocity, note.getVelocity());
  addProp(props, kNoteDelay, note.getDelay());
  doc_->insertChild(node_, kNotesSlot, index, doc_->create("note", std::move(props)));
}

void
PatternView::setNotes(int row, const std::vector<Note> & notes) {
  auto [ first, last ] = rowRange(kNotesSlot, row);
  // Drop the columns that are no longer there, last first so indices hold.
  for (auto i = last; i > first; i--) {
    auto column = static_cast<size_t>(columnOf(ids(kNotesSlot)[i - 1]));
    if (column >= notes.size() || !notes[column].isDefined()) doc_->removeChild(node_, kNotesSlot, i - 1);
  }
  for (size_t column = 0; column < notes.size(); column++) {
    if (notes[column].isDefined()) setNote(row, static_cast<int>(column), notes[column]);
  }
}

int
PatternView::pushNote(int row, Note note) {
  auto [ first, last ] = rowRange(kNotesSlot, row);
  std::set<int> taken;
  for (auto i = first; i < last; i++) taken.insert(columnOf(ids(kNotesSlot)[i]));
  int column = 0;
  while (taken.count(column)) column++;
  setNote(row, column, note);
  return column;
}

void
PatternView::clearNotes(int row) {
  auto [ first, last ] = rowRange(kNotesSlot, row);
  for (auto i = last; i > first; i--) doc_->removeChild(node_, kNotesSlot, i - 1);
}

void
PatternView::deleteNote(int row, int column) {
  auto index = lowerBound(kNotesSlot, row, column);
  auto & list = ids(kNotesSlot);
  if (index < list.size() && rowOf(list[index]) == row && columnOf(list[index]) == column) doc_->removeChild(node_, kNotesSlot, index);
}

void
PatternView::deleteNotesWithValue(int value) {
  for (size_t i = ids(kNotesSlot).size(); i > 0; i--) {
    auto note = noteOf(ids(kNotesSlot)[i - 1]);
    if (note.isDefined() && note.getValue() == value) doc_->removeChild(node_, kNotesSlot, i - 1);
  }
}

void
PatternView::shiftRows(const char * slot, int first_row, int last_row, int delta) {
  auto begin = lowerBound(slot, first_row, 0);
  auto end = lowerBound(slot, last_row + 1, 0);
  // Every row in the range moves by the same amount, so the order holds.
  for (auto i = begin; i < end; i++) {
    auto child = ids(slot)[i];
    doc::set(*doc_, child, kNoteRow, rowOf(child) + delta);
  }
}

void
PatternView::insertRow(int row, int num_rows) {
  for (auto slot : { kNotesSlot, kCommandsSlot }) {
    if (row < num_rows - 1) {
      // The last row's content is pushed off the end; the rest moves down.
      auto [ first, last ] = rowRange(slot, num_rows - 1);
      for (auto i = last; i > first; i--) doc_->removeChild(node_, slot, i - 1);
      shiftRows(slot, row, num_rows - 2, 1);
    } else {
      auto [ first, last ] = rowRange(slot, row);
      for (auto i = last; i > first; i--) doc_->removeChild(node_, slot, i - 1);
    }
  }
}

void
PatternView::deleteRow(int row, int num_rows) {
  for (auto slot : { kNotesSlot, kCommandsSlot }) {
    if (row < num_rows - 1) {
      // The deleted row's content is overwritten; everything below moves up.
      auto [ first, last ] = rowRange(slot, row);
      for (auto i = last; i > first; i--) doc_->removeChild(node_, slot, i - 1);
      shiftRows(slot, row + 1, num_rows - 1, -1);
    } else {
      auto [ first, last ] = rowRange(slot, num_rows - 1);
      for (auto i = last; i > first; i--) doc_->removeChild(node_, slot, i - 1);
    }
  }
}

Note
PatternView::getNote(int row, int note_column) const {
  auto node = findAt(kNotesSlot, row, note_column);
  return node == doc::kNoNode ? Note() : noteOf(node);
}

std::vector<Note>
PatternView::getNotes(int row) const {
  auto [ first, last ] = rowRange(kNotesSlot, row);
  std::vector<Note> out;
  if (first == last) return out;
  out.resize(static_cast<size_t>(columnOf(ids(kNotesSlot)[last - 1])) + 1);
  for (auto i = first; i < last; i++) out[static_cast<size_t>(columnOf(ids(kNotesSlot)[i]))] = noteOf(ids(kNotesSlot)[i]);
  return out;
}

std::map<int, std::vector<Note> >
PatternView::getNotesByRow() const {
  std::map<int, std::vector<Note> > out;
  for (auto child : ids(kNotesSlot)) {
    auto & row = out[rowOf(child)];
    auto column = static_cast<size_t>(columnOf(child));
    if (row.size() <= column) row.resize(column + 1);
    row[column] = noteOf(child);
  }
  return out;
}

void
PatternView::setCommand(int row, int command_column, Command command) {
  if (command_column < 0) return;
  auto index = lowerBound(kCommandsSlot, row, command_column);
  auto & list = ids(kCommandsSlot);
  bool exists = index < list.size() && rowOf(list[index]) == row && columnOf(list[index]) == command_column;
  if (!command.isDefined()) {
    if (exists) doc_->removeChild(node_, kCommandsSlot, index);
    return;
  }
  if (exists) {
    doc::set(*doc_, list[index], kCommandData, to_string(command));
    return;
  }
  std::vector<std::pair<std::string, doc::Value> > props;
  addProp(props, kCommandRow, row);
  addProp(props, kCommandColumn, command_column);
  props.emplace_back(kCommandData.key, doc::toValue(to_string(command)));
  doc_->insertChild(node_, kCommandsSlot, index, doc_->create("command", std::move(props)));
}

int
PatternView::pushCommand(int row, Command command) {
  auto [ first, last ] = rowRange(kCommandsSlot, row);
  std::set<int> taken;
  for (auto i = first; i < last; i++) taken.insert(columnOf(ids(kCommandsSlot)[i]));
  int column = 0;
  while (taken.count(column)) column++;
  setCommand(row, column, command);
  return column;
}

void
PatternView::clearCommands(int row) {
  auto [ first, last ] = rowRange(kCommandsSlot, row);
  for (auto i = last; i > first; i--) doc_->removeChild(node_, kCommandsSlot, i - 1);
}

void
PatternView::deleteCommand(int row, int command_column) {
  auto index = lowerBound(kCommandsSlot, row, command_column);
  auto & list = ids(kCommandsSlot);
  if (index < list.size() && rowOf(list[index]) == row && columnOf(list[index]) == command_column) doc_->removeChild(node_, kCommandsSlot, index);
}

Command
PatternView::getCommand(int row, int command_column) const {
  auto node = findAt(kCommandsSlot, row, command_column);
  return node == doc::kNoNode ? Command() : commandOf(node);
}

std::vector<Command>
PatternView::getCommandsAt(int row) const {
  auto [ first, last ] = rowRange(kCommandsSlot, row);
  std::vector<Command> out;
  if (first == last) return out;
  out.resize(static_cast<size_t>(columnOf(ids(kCommandsSlot)[last - 1])) + 1);
  for (auto i = first; i < last; i++) out[static_cast<size_t>(columnOf(ids(kCommandsSlot)[i]))] = commandOf(ids(kCommandsSlot)[i]);
  return out;
}

std::map<int, std::vector<Command> >
PatternView::getCommandsByRow() const {
  std::map<int, std::vector<Command> > out;
  for (auto child : ids(kCommandsSlot)) {
    auto & row = out[rowOf(child)];
    auto column = static_cast<size_t>(columnOf(child));
    if (row.size() <= column) row.resize(column + 1);
    row[column] = commandOf(child);
  }
  return out;
}

int
PatternView::getContentEnd() const {
  int end = 0;
  if (!ids(kNotesSlot).empty()) end = std::max(end, rowOf(ids(kNotesSlot).back()) + 1);
  if (!ids(kCommandsSlot).empty()) end = std::max(end, rowOf(ids(kCommandsSlot).back()) + 1);
  return end;
}

void
PatternView::updateSubtrackInfo(VisibleTrackInfo & info) const {
  for (auto & [ row, notes ] : getNotesByRow()) info.updateNumSubtracks(static_cast<int>(notes.size()));
}

bool
PatternView::isEmpty() const {
  return ids(kNotesSlot).empty() && ids(kCommandsSlot).empty();
}

bool
PatternView::hasSoundingNote() const {
  for (auto child : ids(kNotesSlot)) {
    auto note = noteOf(child);
    if (note.isDefined() && !note.isOff() && !note.isAftertouch()) return true;
  }
  return false;
}

void
PatternView::assign(const Pattern & pattern) {
  doc::Transaction t(*doc_, "assign pattern");
  setLength(pattern.getLength());
  std::set<int> rows;
  for (auto & [ row, notes ] : getNotesByRow()) rows.insert(row);
  for (auto & [ row, notes ] : pattern.getNotesByRow()) rows.insert(row);
  for (auto row : rows) setNotes(row, pattern.getNotes(row));
  std::set<int> command_rows;
  for (auto & [ row, commands ] : getCommandsByRow()) command_rows.insert(row);
  for (auto & [ row, commands ] : pattern.getCommandsByRow()) command_rows.insert(row);
  for (auto row : command_rows) {
    clearCommands(row);
    auto & commands = pattern.getCommandsAt(row);
    for (size_t column = 0; column < commands.size(); column++) setCommand(row, static_cast<int>(column), commands[column]);
  }
}

Pattern
PatternView::toPattern() const {
  Pattern out;
  out.setLength(getLength());
  for (auto child : ids(kNotesSlot)) out.setNote(rowOf(child), columnOf(child), noteOf(child));
  for (auto child : ids(kCommandsSlot)) out.setCommand(rowOf(child), columnOf(child), commandOf(child));
  return out;
}
