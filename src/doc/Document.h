#ifndef _DOCUMENT_H_
#define _DOCUMENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// The song as a tree of typed nodes with stable ids. Every mutation is one of
// four primitives (setProperty/insertChild/removeChild/moveChild) that record
// both their before and after state into the open transaction, so undo needs
// no knowledge of what a node means. Which undo policy sits on top is not this
// layer's business: the journal is append-only.
namespace doc {

using NodeId = uint32_t;
constexpr NodeId kNoNode = 0;

// monostate = property absent. A property with no schema entry keeps its
// string form, so unknown XML attributes survive a load/save.
using Value = std::variant<std::monostate, int64_t, double, bool, std::string, NodeId>;

struct Slot {
  std::string name;
  std::vector<NodeId> children;
};

struct Node {
  NodeId id = kNoNode;
  std::string type;
  NodeId parent = kNoNode;  // kNoNode while detached
  std::string parent_slot;
  std::vector<std::pair<std::string, Value>> properties;
  std::vector<Slot> slots;

  const Value * find(const std::string & key) const;
  const std::vector<NodeId> * children(const std::string & slot) const;
};

struct Op {
  enum class Kind { SET, INSERT, REMOVE, MOVE } kind = Kind::SET;
  NodeId node = kNoNode;  // SET: the node; others: the parent
  std::string key;        // SET: property key; others: slot name
  Value before, after;    // SET
  NodeId child = kNoNode;  // INSERT/REMOVE: the child
  size_t index = 0;       // INSERT/REMOVE: position; MOVE: from
  size_t to = 0;          // MOVE: destination
};

struct JournalEntry {
  std::vector<Op> ops;
  std::string label;
  uint64_t sequence = 0;
};

// What one committed transaction touched. `structural` is set when any child
// list changed.
struct ChangeSet {
  std::set<NodeId> changed;  // nodes whose properties or child lists changed
  bool structural = false;
};

class Document {
 public:
  Document();
  Document(const Document &) = delete;
  Document & operator=(const Document &) = delete;

  NodeId root() const { return root_; }
  const Node * get(NodeId id) const;
  bool attached(NodeId id) const;  // reachable from the root

  // Allocates a detached node. Not an undoable edit by itself: a detached
  // node is invisible until an insertChild.
  NodeId create(const std::string & type);

  // Id allocation is part of the persisted state (see nextId()/setNextId()).
  NodeId nextId() const { return next_id_; }
  void setNextId(NodeId id) { if (id > next_id_) next_id_ = id; }
  // Loader only: creates a node with a known id (bumps the counter past it).
  NodeId createWithId(NodeId id, const std::string & type);

  // The primitives. Outside an explicit Transaction each call is its own.
  void setProperty(NodeId node, const std::string & key, Value value);
  void insertChild(NodeId parent, const std::string & slot, size_t index, NodeId child);
  NodeId removeChild(NodeId parent, const std::string & slot, size_t index);  // detaches
  void moveChild(NodeId parent, const std::string & slot, size_t from, size_t to);

  // Transactions nest; only the outermost commit appends to the journal.
  void begin(const std::string & label = std::string());
  void commit();
  bool inTransaction() const { return depth_ > 0; }

  const std::vector<JournalEntry> & journal() const { return journal_; }
  // The ops that undo `entry`, in the order to run them. Applying them via the
  // primitives (inside a new transaction) is how a policy layer undoes.
  static std::vector<Op> inverse(const JournalEntry & entry);
  // Replays ops through the primitives.
  void apply(const std::vector<Op> & ops);

  using Listener = std::function<void(const ChangeSet &)>;
  int addListener(Listener fn);
  void removeListener(int handle);

  // Deep copy (nodes only; not the journal or listeners).
  std::unique_ptr<Document> clone() const;
  // Canonical text of the attached tree; for tests and the verifier.
  std::string dump() const;

  // Debug aid: when on, every outermost commit checks that applying the
  // recorded inverse to a clone restores the pre-transaction tree, and that
  // replaying the forward ops again restores the post state. A failure
  // aborts, naming the transaction.
  void setVerify(bool on) { verify_ = on; }

  // Frees detached nodes nothing in the journal refers to; call after
  // trimming history.
  void collectGarbage();
  void trimJournal(size_t keep_last);

 private:
  friend class Transaction;
  Node * mutableGet(NodeId id);
  Slot & slotFor(Node & node, const std::string & name);
  void record(Op op);
  void dumpNode(NodeId id, int depth, std::string & out) const;
  void applyRaw(const Op & op);

  std::unordered_map<NodeId, std::unique_ptr<Node>> nodes_;
  NodeId root_ = kNoNode;
  NodeId next_id_ = 1;
  int depth_ = 0;
  std::string pending_label_;
  std::vector<Op> pending_ops_;
  std::string pre_dump_;
  std::vector<JournalEntry> journal_;
  uint64_t sequence_ = 0;
  std::vector<std::pair<int, Listener>> listeners_;
  int next_listener_ = 1;
  bool verify_ = false;
};

// RAII wrapper: commits on scope exit.
class Transaction {
 public:
  Transaction(Document & d, const std::string & label = std::string()) : doc_(d) { doc_.begin(label); }
  ~Transaction() { doc_.commit(); }
  Transaction(const Transaction &) = delete;
  Transaction & operator=(const Transaction &) = delete;

 private:
  Document & doc_;
};

}  // namespace doc

#endif
