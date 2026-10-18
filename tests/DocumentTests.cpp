#include "TestFramework.h"
#include "doc/Document.h"

using namespace doc;

namespace {

NodeId addNote(Document & d, NodeId parent, size_t index, int row) {
  auto n = d.create("note");
  d.setProperty(n, "row", int64_t(row));
  d.insertChild(parent, "notes", index, n);
  return n;
}

}  // namespace

TEST(document_set_property_undo_restores_value_and_absence) {
  Document d;
  d.setVerify(true);
  d.setProperty(d.root(), "tempo", int64_t(120));
  d.setProperty(d.root(), "tempo", int64_t(90));
  d.setProperty(d.root(), "name", std::string("x"));
  CHECK(d.journal().size() == 3);

  auto before = d.dump();
  d.apply(Document::inverse(d.journal().back()));
  CHECK(d.get(d.root())->find("name") == nullptr);
  d.apply(Document::inverse(d.journal()[1]));
  CHECK(std::get<int64_t>(*d.get(d.root())->find("tempo")) == 120);
  CHECK(d.dump() != before);
}

TEST(document_setting_an_equal_value_records_nothing) {
  Document d;
  d.setProperty(d.root(), "a", int64_t(1));
  auto n = d.journal().size();
  d.setProperty(d.root(), "a", int64_t(1));
  CHECK(d.journal().size() == n);
}

TEST(document_nested_transactions_make_one_journal_entry) {
  Document d;
  d.setVerify(true);
  {
    Transaction outer(d, "outer");
    d.setProperty(d.root(), "a", int64_t(1));
    {
      Transaction inner(d, "inner");
      d.setProperty(d.root(), "b", int64_t(2));
    }
    CHECK(d.journal().empty());
  }
  CHECK(d.journal().size() == 1);
  CHECK(d.journal()[0].label == "outer");
  CHECK(d.journal()[0].ops.size() == 2);
}

TEST(document_remove_detaches_and_undo_reinserts_same_node) {
  Document d;
  d.setVerify(true);
  auto a = addNote(d, d.root(), 0, 4);
  auto b = addNote(d, d.root(), 1, 8);
  auto removed = d.removeChild(d.root(), "notes", 0);
  CHECK(removed == a);
  CHECK(!d.attached(a));
  CHECK(d.get(a) != nullptr);  // detached, not destroyed
  CHECK(d.get(d.root())->children("notes")->size() == 1);

  d.apply(Document::inverse(d.journal().back()));
  auto & kids = *d.get(d.root())->children("notes");
  CHECK(kids.size() == 2 && kids[0] == a && kids[1] == b);
  CHECK(d.attached(a));
}

TEST(document_move_records_as_a_move_and_inverts) {
  Document d;
  d.setVerify(true);
  auto a = addNote(d, d.root(), 0, 1);
  auto b = addNote(d, d.root(), 1, 2);
  auto c = addNote(d, d.root(), 2, 3);
  d.moveChild(d.root(), "notes", 0, 2);
  CHECK(d.journal().back().ops.size() == 1);
  CHECK(d.journal().back().ops[0].kind == Op::Kind::MOVE);
  auto & kids = *d.get(d.root())->children("notes");
  CHECK(kids[0] == b && kids[1] == c && kids[2] == a);
  d.apply(Document::inverse(d.journal().back()));
  CHECK(kids[0] == a && kids[1] == b && kids[2] == c);
}

TEST(document_verifier_accepts_a_mixed_transaction) {
  Document d;
  d.setVerify(true);
  Transaction t(d, "mixed");
  auto track = d.create("track");
  d.insertChild(d.root(), "tracks", 0, track);
  auto n1 = addNote(d, track, 0, 1);
  addNote(d, track, 1, 5);
  d.setProperty(n1, "row", int64_t(2));
  d.moveChild(track, "notes", 0, 1);
  d.removeChild(track, "notes", 0);
}

TEST(document_undo_is_itself_a_recorded_transaction) {
  Document d;
  d.setProperty(d.root(), "a", int64_t(1));
  d.setProperty(d.root(), "a", int64_t(2));
  auto n = d.journal().size();
  d.apply(Document::inverse(d.journal().back()));
  CHECK(d.journal().size() == n + 1);
  // Undoing the undo brings the edit back.
  d.apply(Document::inverse(d.journal().back()));
  CHECK(std::get<int64_t>(*d.get(d.root())->find("a")) == 2);
}

TEST(document_listeners_get_changed_ids_once_per_outer_commit) {
  Document d;
  int calls = 0;
  ChangeSet seen;
  d.addListener([&](const ChangeSet & c) { calls++; seen = c; });
  auto track = d.create("track");
  {
    Transaction t(d);
    d.insertChild(d.root(), "tracks", 0, track);
    d.setProperty(track, "mute", true);
  }
  CHECK(calls == 1);
  CHECK(seen.structural);
  CHECK(seen.changed.count(track) == 1 && seen.changed.count(d.root()) == 1);
}

TEST(document_clone_matches_and_is_independent) {
  Document d;
  auto t = d.create("track");
  d.insertChild(d.root(), "tracks", 0, t);
  d.setProperty(t, "name", std::string("lead"));
  auto c = d.clone();
  CHECK(c->dump() == d.dump());
  d.setProperty(t, "name", std::string("bass"));
  CHECK(c->dump() != d.dump());
}

TEST(document_garbage_collection_keeps_nodes_the_journal_refers_to) {
  Document d;
  auto a = addNote(d, d.root(), 0, 1);
  d.removeChild(d.root(), "notes", 0);
  d.collectGarbage();
  CHECK(d.get(a) != nullptr);  // history can still reinsert it
  d.trimJournal(0);
  d.collectGarbage();
  CHECK(d.get(a) == nullptr);
}

TEST(document_group_is_one_undo_step_but_every_edit_is_visible_at_once) {
  Document d;
  d.setVerify(true);
  auto base = d.journal().size();
  int notifications = 0;
  d.addListener([&](const ChangeSet &) { notifications++; });

  d.beginGroup("take");
  addNote(d, d.root(), 0, 1);
  CHECK(notifications > 0);  // visible before the group ends
  CHECK(d.get(d.root())->children("notes")->size() == 1);
  addNote(d, d.root(), 1, 2);
  addNote(d, d.root(), 2, 3);
  CHECK(d.journal().size() > base + 1);  // not merged yet
  d.endGroup();

  CHECK(d.journal().size() == base + 1);
  CHECK(d.journal().back().label == "take");
  d.apply(Document::inverse(d.journal().back()));
  CHECK(d.get(d.root())->children("notes")->empty());
}

TEST(document_empty_group_leaves_no_entry) {
  Document d;
  auto base = d.journal().size();
  d.beginGroup("take");
  d.endGroup();
  CHECK(d.journal().size() == base);
}

TEST(subtree_revision_moves_when_anything_under_a_node_changes) {
  doc::Document d;
  auto parent = d.create("p");
  d.insertChild(d.root(), "kids", 0, parent);
  auto child = d.create("c");
  d.insertChild(parent, "kids", 0, child);
  auto before = d.subtreeRevision(parent);
  d.setProperty(child, "x", doc::Value(int64_t(1)));
  auto after_set = d.subtreeRevision(parent);
  CHECK(after_set > before);
  CHECK(d.subtreeRevision(d.root()) == after_set);
  auto sibling = d.create("c");
  d.insertChild(parent, "kids", 1, sibling);
  auto after_insert = d.subtreeRevision(parent);
  CHECK(after_insert > after_set);
  d.moveChild(parent, "kids", 0, 1);
  CHECK(d.subtreeRevision(parent) > after_insert);
  auto stable = d.subtreeRevision(parent);
  auto other = d.create("o");
  d.insertChild(d.root(), "kids", 1, other);
  CHECK(d.subtreeRevision(parent) == stable); // a change elsewhere leaves it alone
  CHECK(d.subtreeRevision(doc::kNoNode) == 0);
}
