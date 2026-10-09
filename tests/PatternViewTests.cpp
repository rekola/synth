#include "TestFramework.h"

#include "../src/model/PatternView.h"

#include "TestRandom.h"

namespace {

struct Pair {
  doc::Document document;
  Pattern reference;
  PatternView view;
  Pair() {
    document.setVerify(true);
    auto node = PatternView::create(document);
    document.insertChild(document.root(), "patterns", 0, node);
    view = PatternView(&document, node);
  }
};

std::map<int, std::vector<Note> > notesOf(const Pattern & p) {
  std::map<int, std::vector<Note> > out;
  for (auto & [ row, notes ] : p.getNotesByRow()) out[row] = notes;
  return out;
}

std::map<int, std::vector<Command> > commandsOf(const Pattern & p) {
  std::map<int, std::vector<Command> > out;
  for (auto & [ row, commands ] : p.getCommandsByRow()) out[row] = commands;
  return out;
}

bool same(const Note & a, const Note & b) {
  return a.isDefined() == b.isDefined() && (!a.isDefined() || (a.getValue() == b.getValue() && a.getVelocity() == b.getVelocity() && a.getDelay() == b.getDelay()));
}

bool sameNotes(const std::vector<Note> & a, const std::vector<Note> & b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) if (!same(a[i], b[i])) return false;
  return true;
}

bool agree(const Pair & p) {
  auto want = notesOf(p.reference), got = p.view.getNotesByRow();
  if (want.size() != got.size()) return false;
  for (auto & [ row, notes ] : want) {
    auto it = got.find(row);
    if (it == got.end() || !sameNotes(notes, it->second)) return false;
  }
  auto want_commands = commandsOf(p.reference), got_commands = p.view.getCommandsByRow();
  if (want_commands.size() != got_commands.size()) return false;
  for (auto & [ row, commands ] : want_commands) {
    auto it = got_commands.find(row);
    if (it == got_commands.end() || commands.size() != it->second.size()) return false;
    for (size_t i = 0; i < commands.size(); i++) if (to_string(commands[i]) != to_string(it->second[i])) return false;
  }
  return p.view.getContentEnd() == p.reference.getContentEnd() && p.view.isEmpty() == p.reference.isEmpty() &&
    p.view.hasSoundingNote() == p.reference.hasSoundingNote() && p.view.getLength() == p.reference.getLength();
}

Note randomNote(TestRng & rng) {
  switch (rng() % 5) {
    case 0: return Note(static_cast<int>(rng() % 40), 0, static_cast<short>(rng() % 4)); // off
    case 1: return Note(-1, static_cast<short>(1 + rng() % 127)); // aftertouch
    default: return Note(static_cast<int>(rng() % 40), static_cast<short>(1 + rng() % 127), static_cast<short>(rng() % 200));
  }
}

Command randomCommand(TestRng & rng) {
  static const char * kCommands[] = { "0U10", "ZB02", "YL04", "0LEC", "1V7F" };
  return Command(kCommands[rng() % 5]);
}

}  // namespace

TEST(pattern_view_matches_the_value_pattern_under_random_edits) {
  TestRng rng(12345);
  Pair p;
  for (int step = 0; step < 4000; step++) {
    int row = static_cast<int>(rng() % 14), column = static_cast<int>(rng() % 4);
    switch (rng() % 15) {
      case 0: case 1: { auto n = randomNote(rng); p.reference.setNote(row, column, n); p.view.setNote(row, column, n); break; }
      case 2: { auto n = Note(); p.reference.setNote(row, column, n); p.view.setNote(row, column, n); break; }
      case 3: { std::vector<Note> notes; for (auto c = rng() % 4; c > 0; c--) notes.push_back(rng() % 3 ? randomNote(rng) : Note()); p.reference.setNotes(row, notes); p.view.setNotes(row, notes); break; }
      case 4: { auto n = randomNote(rng); CHECK(p.reference.pushNote(row, n) == p.view.pushNote(row, n)); break; }
      case 5: p.reference.clearNotes(row); p.view.clearNotes(row); break;
      case 6: p.reference.deleteNote(row, column); p.view.deleteNote(row, column); break;
      case 7: { auto value = static_cast<int>(rng() % 40); p.reference.deleteNotesWithValue(value); p.view.deleteNotesWithValue(value); break; }
      case 8: { int rows = 4 + static_cast<int>(rng() % 12); p.reference.insertRow(row, rows); p.view.insertRow(row, rows); break; }
      case 9: { int rows = 4 + static_cast<int>(rng() % 12); p.reference.deleteRow(row, rows); p.view.deleteRow(row, rows); break; }
      case 10: { auto c = rng() % 4 ? randomCommand(rng) : Command(); p.reference.setCommand(row, column, c); p.view.setCommand(row, column, c); break; }
      case 11: { auto c = randomCommand(rng); CHECK(p.reference.pushCommand(row, c) == p.view.pushCommand(row, c)); break; }
      case 12: p.reference.clearCommands(row); p.view.clearCommands(row); break;
      case 13: p.reference.deleteCommand(row, column); p.view.deleteCommand(row, column); break;
      default: { int length = static_cast<int>(rng() % 3) * 8; p.reference.setLength(length); p.view.setLength(length); break; }
    }
    if (!agree(p)) {
      std::fprintf(stderr, "  diverged at step %d\n", step);
      CHECK(false);
      return;
    }
    // Point reads agree with the maps.
    if (step % 50 == 0) {
      for (int r = 0; r < 16; r++) {
        CHECK(sameNotes(p.reference.getNotes(r), p.view.getNotes(r)));
        CHECK(same(p.reference.getNote(r, 1), p.view.getNote(r, 1)));
        CHECK(to_string(p.reference.getCommand(r, 0)) == to_string(p.view.getCommand(r, 0)));
      }
    }
  }
}

TEST(pattern_view_to_pattern_and_assign_round_trip) {
  TestRng rng(7);
  Pattern source;
  for (int i = 0; i < 60; i++) source.setNote(static_cast<int>(rng() % 20), static_cast<int>(rng() % 3), randomNote(rng));
  for (int i = 0; i < 10; i++) source.setCommand(static_cast<int>(rng() % 20), static_cast<int>(rng() % 2), randomCommand(rng));
  source.setLength(16);

  Pair p;
  p.view.setNote(3, 0, Note(1, 5)); // something to be replaced
  p.view.assign(source);
  p.reference = source;
  CHECK(agree(p));
  auto back = p.view.toPattern();
  Pair q;
  q.reference = back;
  q.view.assign(back);
  CHECK(agree(q));
  CHECK(notesOf(back).size() == notesOf(source).size());
}

TEST(pattern_view_edits_are_undone_by_the_journal) {
  Pair p;
  p.view.setNote(2, 0, Note(10, 100));
  p.view.setNote(2, 1, Note(12, 100));
  auto before = p.document.dump();
  {
    doc::Transaction t(p.document, "edit");
    p.view.setNotes(2, { Note(11, 90) });
    p.view.insertRow(0, 8);
    p.view.setCommand(5, Command("0U10"));
  }
  CHECK(p.document.dump() != before);
  p.document.apply(doc::Document::inverse(p.document.journal().back()));
  CHECK(p.document.dump() == before);
}

TEST(pattern_view_keeps_its_nodes_sorted_by_row_then_column) {
  Pair p;
  p.view.setNote(9, 0, Note(1, 5));
  p.view.setNote(2, 1, Note(2, 5));
  p.view.setNote(2, 0, Note(3, 5));
  p.view.setNote(5, 0, Note(4, 5));
  auto rows = p.view.getNotesByRow();
  CHECK(rows.begin()->first == 2 && rows.rbegin()->first == 9);
  CHECK(p.view.getNotes(2).size() == 2 && p.view.getNotes(2)[0].getValue() == 3);
}
