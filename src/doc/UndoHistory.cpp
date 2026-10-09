#include "UndoHistory.h"

#include <algorithm>

namespace doc {

const JournalEntry * UndoHistory::find(const Document & document, uint64_t sequence) {
  auto & journal = document.journal();
  auto it = std::lower_bound(journal.begin(), journal.end(), sequence,
			     [](const JournalEntry & e, uint64_t s) { return e.sequence < s; });
  return it != journal.end() && it->sequence == sequence ? &*it : nullptr;
}

const JournalEntry * UndoHistory::newestTracked(const Document & document) {
  auto & journal = document.journal();
  for (auto it = journal.rbegin(); it != journal.rend(); ++it)
    if (it->tracked) return &*it;
  return nullptr;
}

const JournalEntry * UndoHistory::trackedBefore(const Document & document, uint64_t sequence) {
  auto & journal = document.journal();
  for (auto it = journal.rbegin(); it != journal.rend(); ++it)
    if (it->tracked && it->sequence < sequence) return &*it;
  return nullptr;
}

bool UndoHistory::chainIntact(const Document & document) const {
  if (tail_ == 0) return false;
  auto newest = newestTracked(document);
  return newest && newest->sequence == tail_;
}

bool UndoHistory::canUndo(const Document & document) const {
  if (chainIntact(document)) return next_ != 0 && find(document, next_);
  return newestTracked(document) != nullptr;
}

bool UndoHistory::canRedo(const Document & document) const {
  return chainIntact(document) && !chain_.empty();
}

static uint64_t lastSequence(const Document & document) {
  return document.journal().empty() ? 0 : document.journal().back().sequence;
}

bool UndoHistory::undo(Document & document) {
  if (document.inGroup()) return false;
  if (!chainIntact(document)) {
    chain_.clear();
    tail_ = 0;
    auto newest = newestTracked(document);
    next_ = newest ? newest->sequence : 0;
  }
  auto target = next_ ? find(document, next_) : nullptr;
  if (!target) return false;
  pending_ = Pending::UNDO;
  pending_target_ = target->sequence;
  before_ = lastSequence(document);
  document.apply(Document::inverse(*target));
  return true;
}

bool UndoHistory::redo(Document & document) {
  if (document.inGroup() || !canRedo(document)) return false;
  auto undo_entry = find(document, chain_.back().undo_sequence);
  if (!undo_entry) return false;
  pending_ = Pending::REDO;
  pending_target_ = chain_.back().undone_sequence;
  before_ = lastSequence(document);
  document.apply(Document::inverse(*undo_entry));
  return true;
}

void UndoHistory::finish(const Document & document) {
  auto pending = pending_;
  pending_ = Pending::NONE;
  if (pending == Pending::NONE || lastSequence(document) == before_) return;
  tail_ = lastSequence(document);
  if (pending == Pending::UNDO) {
    chain_.push_back({tail_, pending_target_});
    auto earlier = trackedBefore(document, pending_target_);
    next_ = earlier ? earlier->sequence : 0;
  } else {
    // The redo put back what the undo took away; the walk carries on from it.
    chain_.pop_back();
    next_ = pending_target_;
  }
}

}  // namespace doc
