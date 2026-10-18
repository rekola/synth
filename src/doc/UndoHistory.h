#pragma once

#include "Document.h"

#include <cstdint>
#include <vector>

namespace doc {

// The Emacs undo policy over a document's journal. An undo appends the
// inverse of an earlier entry as a new entry, so nothing is ever dropped from
// history; undoing an undo is therefore a redo. Consecutive undos walk back
// through history; the walk is broken by a tracked edit committed since (a
// cursor move or scroll is no edit, so it never breaks it).
//
// The history only decides which entry to invert and applies it; the caller
// opens the scope around it (Song::Edit) and passes the document in.
class UndoHistory {
 public:
  // True if an undo would do something.
  bool canUndo(const Document & document) const;
  // True while the newest tracked entries are an unbroken run of undos.
  bool canRedo(const Document & document) const;

  // Each applies one inverse through the document's primitives and returns
  // false if there was nothing to do. The caller wraps the call in a
  // transaction (Song::Edit) and calls finish() once it has closed, which
  // is when the inverse becomes a journal entry the chain can point at.
  // The ops an undo or redo would apply, without applying them (empty if there
  // is nothing to do).
  std::vector<Op> peekUndo(const Document & document) const;
  std::vector<Op> peekRedo(const Document & document) const;

  bool undo(Document & document);
  bool redo(Document & document);
  void finish(const Document & document);
  // The ops the last undo or redo applied, in the order they ran.
  const std::vector<Op> & lastOps() const { return last_ops_; }

 private:
  struct Link {
    uint64_t undo_sequence;   // the entry the undo appended
    uint64_t undone_sequence; // the entry it inverted
  };

  static const JournalEntry * find(const Document & document, uint64_t sequence);
  static const JournalEntry * newestTracked(const Document & document);
  static const JournalEntry * trackedBefore(const Document & document, uint64_t sequence);
  bool chainIntact(const Document & document) const;

  enum class Pending { NONE, UNDO, REDO };
  std::vector<Op> last_ops_;
  Pending pending_ = Pending::NONE;
  uint64_t pending_target_ = 0;
  uint64_t before_ = 0;
  std::vector<Link> chain_;  // the run of undos not yet redone, oldest first
  uint64_t tail_ = 0;        // the newest entry the chain itself appended
  uint64_t next_ = 0;        // the entry the next undo inverts; 0 = none left
};

}  // namespace doc
