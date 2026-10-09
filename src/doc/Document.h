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
  // Bumped whenever this node's properties or child lists change (undo and
  // redo included), from one counter for the whole document, so a larger
  // revision always means a later change.
  uint64_t revision = 0;

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
  // False for changes that follow the audio thread rather than the user (a
  // scene launch setting the tempo, a glide landing in the model): they
  // stay in the journal so it is complete, but an undo policy skips them.
  bool tracked = true;
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
  // A document nothing is ever attached to, for a default-constructed view to
  // point at: reads find nothing, writes go nowhere.
  static Document & inert();
  const Node * get(NodeId id) const;
  bool attached(NodeId id) const;  // reachable from the root
  // The newest revision anywhere in the subtree under `id` (0 for no such
  // node): unchanged means nothing under it changed, so anything compiled
  // from it can be reused.
  uint64_t subtreeRevision(NodeId id) const;

  // Allocates a detached node. Not an undoable edit by itself: a detached
  // node is invisible until an insertChild. Changes to a node (or a parent)
  // that is not attached are not recorded either, for the same reason - a
  // subtree is built off to the side, and attaching it is the one edit.
  NodeId create(const std::string & type);
  // Allocates a detached node that already has its properties - not
  // recorded, since an invisible node's history is nobody's business; the
  // insertChild that attaches it is.
  NodeId create(const std::string & type, std::vector<std::pair<std::string, Value> > properties);

  // Builds a detached subtree: appends `child` to a node that is itself not
  // attached yet. Not recorded - it is part of making the node, and the
  // insertChild that attaches the whole subtree is what history sees.
  void appendToDetached(NodeId parent, const std::string & slot, NodeId child);

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

  // Called around a primitive that runs with no transaction open, so its
  // owner can make that write a complete action of its own (open a scope
  // before, close it after) instead of leaving it half-published.
  struct ImplicitScope {
    virtual ~ImplicitScope() { }
    virtual void begin() = 0;
    virtual void end() = 0;
  };
  void setImplicitScope(ImplicitScope * scope) { implicit_scope_ = scope; }

  // Transactions nest; only the outermost commit appends to the journal.
  void begin(const std::string & label = std::string(), bool tracked = true);
  void commit();
  bool inTransaction() const { return depth_ > 0; }

  // An undo group folds every transaction committed while it is open into one
  // journal entry, for an action that lasts a while but is one undo step - a
  // live recording take. Each transaction inside still commits at once:
  // listeners fire and the edit is visible (and audible) right away. Only
  // the history is merged, when the group ends. Anything else committed
  // meanwhile joins the group.
  void beginGroup(const std::string & label);
  void endGroup();
  bool inGroup() const { return in_group_; }

  const std::vector<JournalEntry> & journal() const { return journal_; }
  // Forgets all history, for a document that was just loaded: opening a file
  // is not an undoable edit.
  void clearJournal() { journal_.clear(); }
  // The ops that undo `entry`, in the order to run them. Applying them via the
  // primitives (inside a new transaction) is how a policy layer undoes.
  static std::vector<Op> inverse(const JournalEntry & entry);
  // Replays ops through the primitives.
  void apply(const std::vector<Op> & ops);

  using Listener = std::function<void(const ChangeSet &)>;
  int addListener(Listener fn);
  void removeListener(int handle);

  // Visits every node in the table, attached or not.
  template <typename Fn>
  void forEachNode(Fn && fn) const {
    for (auto & entry : nodes_) fn(*entry.second);
  }
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
  struct ScopeGuard;
  Node * mutableGet(NodeId id);
  void touch(Node & node) { node.revision = ++revision_counter_; }
  Slot & slotFor(Node & node, const std::string & name);
  void record(Op op);
  void dumpNode(NodeId id, int depth, std::string & out) const;
  void applyRaw(const Op & op);

  std::unordered_map<NodeId, std::unique_ptr<Node>> nodes_;
  NodeId root_ = kNoNode;
  NodeId next_id_ = 1;
  uint64_t revision_counter_ = 0;
  int depth_ = 0;
  std::string pending_label_;
  bool pending_tracked_ = true;
  std::vector<Op> pending_ops_;
  std::string pre_dump_;
  std::vector<JournalEntry> journal_;
  uint64_t sequence_ = 0;
  std::vector<std::pair<int, Listener>> listeners_;
  int next_listener_ = 1;
  bool verify_ = false;
  ImplicitScope * implicit_scope_ = nullptr;
  bool in_group_ = false;
  size_t group_start_ = 0;
  std::string group_label_;
};

// RAII wrapper: commits on scope exit.
class Transaction {
 public:
  Transaction(Document & d, const std::string & label = std::string(), bool tracked = true) : doc_(d) { doc_.begin(label, tracked); }
  ~Transaction() { doc_.commit(); }
  Transaction(const Transaction &) = delete;
  Transaction & operator=(const Transaction &) = delete;

 private:
  Document & doc_;
};

}  // namespace doc

#endif
