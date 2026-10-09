#include "Document.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace doc {

// Runs the owner's implicit scope around a primitive called with no
// transaction open.
struct Document::ScopeGuard {
  Document & d;
  bool active;
  explicit ScopeGuard(Document & doc) : d(doc), active(doc.depth_ == 0 && doc.implicit_scope_) { if (active) d.implicit_scope_->begin(); }
  ~ScopeGuard() { if (active) d.implicit_scope_->end(); }
};

Document & Document::inert() {
  static Document document;
  return document;
}

const Value * Node::find(const std::string & key) const {
  for (auto & [ k, v ] : properties)
    if (k == key) return &v;
  return nullptr;
}

const std::vector<NodeId> * Node::children(const std::string & slot) const {
  for (auto & s : slots)
    if (s.name == slot) return &s.children;
  return nullptr;
}

Document::Document() {
  root_ = create("song");
  // The root is attached by definition.
}

const Node * Document::get(NodeId id) const {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second.get();
}

Node * Document::mutableGet(NodeId id) {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second.get();
}

bool Document::attached(NodeId id) const {
  while (id != kNoNode) {
    if (id == root_) return true;
    auto n = get(id);
    if (!n) return false;
    id = n->parent;
  }
  return false;
}

uint64_t Document::subtreeRevision(NodeId id) const {
  auto n = get(id);
  if (!n) return 0;
  uint64_t newest = n->revision;
  for (auto & slot : n->slots)
    for (auto child : slot.children) newest = std::max(newest, subtreeRevision(child));
  return newest;
}

NodeId Document::create(const std::string & type) {
  return createWithId(next_id_, type);
}

NodeId Document::create(const std::string & type, std::vector<std::pair<std::string, Value> > properties) {
  auto id = create(type);
  auto node = mutableGet(id);
  for (auto & p : properties)
    if (!std::holds_alternative<std::monostate>(p.second)) node->properties.push_back(std::move(p));
  return id;
}

NodeId Document::createWithId(NodeId id, const std::string & type) {
  assert(id != kNoNode && !nodes_.count(id));
  auto node = std::make_unique<Node>();
  node->id = id;
  node->type = type;
  nodes_[id] = std::move(node);
  if (id >= next_id_) next_id_ = id + 1;
  return id;
}

void Document::appendToDetached(NodeId parent_id, const std::string & slot, NodeId child_id) {
  auto parent = mutableGet(parent_id);
  auto child = mutableGet(child_id);
  assert(parent && child && !attached(parent_id) && child->parent == kNoNode);
  auto & s = slotFor(*parent, slot);
  s.children.push_back(child_id);
  touch(*parent);
  child->parent = parent_id;
  child->parent_slot = slot;
}

Slot & Document::slotFor(Node & node, const std::string & name) {
  for (auto & s : node.slots)
    if (s.name == name) return s;
  node.slots.push_back({ name, {} });
  return node.slots.back();
}

void Document::record(Op op) {
  pending_ops_.push_back(std::move(op));
}

void Document::setProperty(NodeId id, const std::string & key, Value value) {
  ScopeGuard scope(*this);
  Transaction t(*this);
  auto node = mutableGet(id);
  assert(node);
  if (!node) return;
  auto it = std::find_if(node->properties.begin(), node->properties.end(), [&](auto & p) { return p.first == key; });
  Value before = it == node->properties.end() ? Value() : it->second;
  if (before == value) return;
  Op op;
  op.kind = Op::Kind::SET;
  op.node = id;
  op.key = key;
  op.before = before;
  op.after = value;
  touch(*node);
  if (std::holds_alternative<std::monostate>(value)) {
    if (it != node->properties.end()) node->properties.erase(it);
  } else if (it != node->properties.end()) {
    it->second = std::move(value);
  } else {
    node->properties.emplace_back(key, std::move(value));
  }
  if (attached(id)) record(std::move(op));
}

void Document::insertChild(NodeId parent_id, const std::string & slot, size_t index, NodeId child_id) {
  ScopeGuard scope(*this);
  Transaction t(*this);
  auto parent = mutableGet(parent_id);
  auto child = mutableGet(child_id);
  assert(parent && child && child->parent == kNoNode && child_id != root_);
  if (!parent || !child) return;
  auto & s = slotFor(*parent, slot);
  assert(index <= s.children.size());
  s.children.insert(s.children.begin() + static_cast<std::ptrdiff_t>(index), child_id);
  touch(*parent);
  child->parent = parent_id;
  child->parent_slot = slot;
  Op op;
  op.kind = Op::Kind::INSERT;
  op.node = parent_id;
  op.key = slot;
  op.child = child_id;
  op.index = index;
  if (attached(parent_id)) record(std::move(op));
}

NodeId Document::removeChild(NodeId parent_id, const std::string & slot, size_t index) {
  ScopeGuard scope(*this);
  Transaction t(*this);
  auto parent = mutableGet(parent_id);
  assert(parent);
  if (!parent) return kNoNode;
  auto & s = slotFor(*parent, slot);
  assert(index < s.children.size());
  auto child_id = s.children[index];
  s.children.erase(s.children.begin() + static_cast<std::ptrdiff_t>(index));
  touch(*parent);
  auto child = mutableGet(child_id);
  child->parent = kNoNode;
  child->parent_slot.clear();
  Op op;
  op.kind = Op::Kind::REMOVE;
  op.node = parent_id;
  op.key = slot;
  op.child = child_id;
  op.index = index;
  if (attached(parent_id)) record(std::move(op));
  return child_id;
}

void Document::moveChild(NodeId parent_id, const std::string & slot, size_t from, size_t to) {
  if (from == to) return;
  ScopeGuard scope(*this);
  Transaction t(*this);
  auto parent = mutableGet(parent_id);
  assert(parent);
  if (!parent) return;
  auto & s = slotFor(*parent, slot);
  assert(from < s.children.size() && to < s.children.size());
  auto id = s.children[from];
  s.children.erase(s.children.begin() + static_cast<std::ptrdiff_t>(from));
  s.children.insert(s.children.begin() + static_cast<std::ptrdiff_t>(to), id);
  touch(*parent);
  Op op;
  op.kind = Op::Kind::MOVE;
  op.node = parent_id;
  op.key = slot;
  op.index = from;
  op.to = to;
  if (attached(parent_id)) record(std::move(op));
}

void Document::begin(const std::string & label, bool tracked) {
  if (depth_++ == 0) {
    pending_label_ = label;
    pending_tracked_ = tracked;
    pending_ops_.clear();
    if (verify_) pre_dump_ = dump();
  } else if (pending_label_.empty()) {
    pending_label_ = label;
  }
}

void Document::commit() {
  assert(depth_ > 0);
  if (--depth_ > 0) return;
  if (pending_ops_.empty()) return;

  JournalEntry entry;
  entry.ops = std::move(pending_ops_);
  entry.label = std::move(pending_label_);
  entry.sequence = ++sequence_;
  entry.tracked = pending_tracked_;
  pending_ops_.clear();
  pending_label_.clear();

  if (verify_) {
    auto post = dump();
    auto copy = clone();
    copy->apply(inverse(entry));
    if (copy->dump() != pre_dump_) {
      std::fprintf(stderr, "doc: undo record for \"%s\" does not restore the pre-transaction tree\n--- expected\n%s\n--- got\n%s\n", entry.label.c_str(), pre_dump_.c_str(), copy->dump().c_str());
      std::abort();
    }
    copy->apply(entry.ops);
    if (copy->dump() != post) {
      std::fprintf(stderr, "doc: redo of \"%s\" does not reproduce the post-transaction tree\n", entry.label.c_str());
      std::abort();
    }
  }

  ChangeSet changes;
  for (auto & op : entry.ops) {
    changes.changed.insert(op.node);
    if (op.kind != Op::Kind::SET) {
      changes.structural = true;
      if (op.child != kNoNode) changes.changed.insert(op.child);
    }
  }
  journal_.push_back(std::move(entry));
  // Listeners may start transactions of their own.
  auto listeners = listeners_;
  for (auto & [ handle, fn ] : listeners) fn(changes);
}

void Document::beginGroup(const std::string & label) {
  assert(!in_group_ && depth_ == 0);
  in_group_ = true;
  group_start_ = journal_.size();
  group_label_ = label;
}

void Document::endGroup() {
  assert(in_group_);
  in_group_ = false;
  if (journal_.size() <= group_start_ + 1) {
    if (journal_.size() == group_start_ + 1 && journal_.back().tracked) journal_.back().label = group_label_;
    return;
  }
  // Untracked entries (the audio thread's mirror writes) stay entries of
  // their own, before the merged one, so undoing the group never undoes them.
  JournalEntry merged;
  merged.label = group_label_;
  std::vector<JournalEntry> untracked;
  for (auto i = group_start_; i < journal_.size(); i++) {
    if (journal_[i].tracked)
      merged.ops.insert(merged.ops.end(), journal_[i].ops.begin(), journal_[i].ops.end());
    else
      untracked.push_back(std::move(journal_[i]));
  }
  journal_.erase(journal_.begin() + static_cast<std::ptrdiff_t>(group_start_), journal_.end());
  for (auto & entry : untracked) journal_.push_back(std::move(entry));
  if (merged.ops.empty()) return;
  merged.sequence = ++sequence_;
  journal_.push_back(std::move(merged));
}

std::vector<Op> Document::inverse(const JournalEntry & entry) {
  std::vector<Op> out;
  out.reserve(entry.ops.size());
  for (auto it = entry.ops.rbegin(); it != entry.ops.rend(); ++it) {
    Op op = *it;
    switch (it->kind) {
      case Op::Kind::SET:
        op.before = it->after;
        op.after = it->before;
        break;
      case Op::Kind::INSERT:
        op.kind = Op::Kind::REMOVE;
        break;
      case Op::Kind::REMOVE:
        op.kind = Op::Kind::INSERT;
        break;
      case Op::Kind::MOVE:
        op.index = it->to;
        op.to = it->index;
        break;
    }
    out.push_back(std::move(op));
  }
  return out;
}

void Document::applyRaw(const Op & op) {
  switch (op.kind) {
    case Op::Kind::SET: setProperty(op.node, op.key, op.after); break;
    case Op::Kind::INSERT: insertChild(op.node, op.key, op.index, op.child); break;
    case Op::Kind::REMOVE: removeChild(op.node, op.key, op.index); break;
    case Op::Kind::MOVE: moveChild(op.node, op.key, op.index, op.to); break;
  }
}

void Document::apply(const std::vector<Op> & ops) {
  ScopeGuard scope(*this);
  Transaction t(*this);
  for (auto & op : ops) applyRaw(op);
}

int Document::addListener(Listener fn) {
  auto handle = next_listener_++;
  listeners_.emplace_back(handle, std::move(fn));
  return handle;
}

void Document::removeListener(int handle) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [&](auto & p) { return p.first == handle; }), listeners_.end());
}

std::unique_ptr<Document> Document::clone() const {
  auto copy = std::make_unique<Document>();
  copy->nodes_.clear();
  for (auto & [ id, node ] : nodes_) copy->nodes_[id] = std::make_unique<Node>(*node);
  copy->root_ = root_;
  copy->next_id_ = next_id_;
  copy->revision_counter_ = revision_counter_;
  return copy;
}

static void formatValue(const Value & v, std::string & out) {
  if (auto i = std::get_if<int64_t>(&v)) out += "i" + std::to_string(*i);
  else if (auto d = std::get_if<double>(&v)) { char buf[48]; std::snprintf(buf, sizeof buf, "d%.17g", *d); out += buf; }
  else if (auto b = std::get_if<bool>(&v)) out += *b ? "btrue" : "bfalse";
  else if (auto s = std::get_if<std::string>(&v)) out += "s\"" + *s + "\"";
  else if (auto n = std::get_if<NodeId>(&v)) out += "n" + std::to_string(*n);
  else out += "-";
}

void Document::dumpNode(NodeId id, int depth, std::string & out) const {
  auto n = get(id);
  out.append(static_cast<size_t>(depth) * 2, ' ');
  out += n->type + "#" + std::to_string(id);
  auto props = n->properties;
  std::sort(props.begin(), props.end(), [](auto & a, auto & b) { return a.first < b.first; });
  for (auto & [ k, v ] : props) {
    out += " " + k + "=";
    formatValue(v, out);
  }
  out += "\n";
  auto slots = n->slots;
  std::sort(slots.begin(), slots.end(), [](auto & a, auto & b) { return a.name < b.name; });
  for (auto & s : slots) {
    if (s.children.empty()) continue;
    out.append(static_cast<size_t>(depth) * 2 + 1, ' ');
    out += "[" + s.name + "]\n";
    for (auto c : s.children) dumpNode(c, depth + 1, out);
  }
}

std::string Document::dump() const {
  std::string out;
  dumpNode(root_, 0, out);
  return out;
}

void Document::trimJournal(size_t keep_last) {
  if (journal_.size() > keep_last)
    journal_.erase(journal_.begin(), journal_.end() - static_cast<std::ptrdiff_t>(keep_last));
}

void Document::collectGarbage() {
  std::set<NodeId> live;
  std::vector<NodeId> stack = { root_ };
  for (auto & e : journal_)
    for (auto & op : e.ops) {
      if (op.child != kNoNode) stack.push_back(op.child);
      stack.push_back(op.node);
    }
  while (!stack.empty()) {
    auto id = stack.back();
    stack.pop_back();
    if (!live.insert(id).second) continue;
    auto n = get(id);
    if (!n) continue;
    for (auto & s : n->slots)
      for (auto c : s.children) stack.push_back(c);
    for (auto & [ k, v ] : n->properties)
      if (auto ref = std::get_if<NodeId>(&v)) stack.push_back(*ref);
  }
  for (auto it = nodes_.begin(); it != nodes_.end();) {
    if (!live.count(it->first)) it = nodes_.erase(it);
    else ++it;
  }
}

}  // namespace doc
