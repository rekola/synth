#pragma once

#include <cellui/Color.h>
#include <cellui/EventTypes.h>
#include <cellui/CommandRegistry.h>
#include <cellui/Input.h>
#include <cellui/KeyChord.h>
#include <cellui/KeyChordNames.h>
#include <cellui/Keymap.h>
#include <cellui/Plane.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cellui {

// A widget: owns a plane, draws through it, and dispatches key chords to
// named commands.
class Widget {
 public:
  explicit Widget() { table_->widget = this; }
  explicit Widget(Plane & parent) {
    table_->widget = this;
    setPlane(parent.createChild());
  }
  virtual ~Widget() {
    if (plane_) plane_->setOwner(nullptr);
    table_->kill();
    detach();
    // The children outlive it as roots of their own trees.
    std::vector<Widget *> orphans = std::move(children_);
    children_.clear();
    for (Widget * child : orphans) {
      child->parent_ = nullptr;
      child->epoch_++;
      registerSubtree(*child);
    }
  }

  // --- The widget tree. Links only: a widget does not own its children, and
  // one that goes away leaves its children as roots of their own trees.
  Widget * parent() const { return parent_; }
  const std::vector<Widget *> & children() const { return children_; }
  // Makes this the last child of `parent`, leaving its current parent.
  void attach(Widget & parent) {
    detach();
    parent_ = &parent;
    parent.children_.push_back(this);
    root().epoch_++;
    registerSubtree(*this);
  }
  void detach() {
    if (!parent_) return;
    Widget & old_root = root();
    auto & siblings = parent_->children_;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    parent_ = nullptr;
    old_root.epoch_++;
    epoch_++;
    registerSubtree(*this);
  }
  Widget & root() {
    Widget * widget = this;
    while (widget->parent_) widget = widget->parent_;
    return *widget;
  }

  // --- Posted events (see EventTypes.h). An event is any struct.

  // Installs a handler for events of type E; it stays until the Connection
  // is destroyed or disconnected (or this widget goes). A bubble handler
  // also runs for a root post; `phase` only matters to a targeted one.
  template <class E>
  [[nodiscard]] Connection on(std::function<Handled(const E &, EventContext &)> fn, Phase phase = Phase::bubble) {
    uint64_t id = table_->add(typeKey<E>(), phase, [fn = std::move(fn)](const void * event, EventContext & ctx) {
      return fn(*static_cast<const E *>(event), ctx);
    });
    root().registry().add(typeKey<E>(), table_);
    return detail::ConnectionAccess::make(table_, id);
  }

  // Targeted: capture handlers from the root down to this widget (each may
  // intercept by returning Handled::yes), then bubble handlers from this
  // widget up to the root, until one returns Handled::yes. Returns whether
  // some handler did.
  template <class E>
  Handled post(const E & event) {
    return dispatchTargeted(typeKey<E>(), &event);
  }

  // Untargeted: every handler for E in the tree, in tree pre-order
  // (ancestors before descendants, siblings in child order, a widget's own
  // in the order they were installed), until one returns Handled::yes.
  // Capture and bubble handlers alike run, with the phase set to bubble.
  template <class E>
  Handled postToRoot(const E & event) {
    return root().dispatchRoot(typeKey<E>(), &event);
  }

  // Only this widget's own handlers, capture and bubble alike, no ancestors:
  // for events that do not travel, such as Enter and Leave.
  template <class E>
  Handled postDirect(const E & event) {
    EventContext ctx;
    ctx.source = this;
    ctx.time = now();
    ctx.phase = Phase::bubble;
    auto table = table_;
    if (!table->widget) return Handled::no;
    Handled handled = table->run(typeKey<E>(), std::nullopt, &event, ctx);
    return handled;
  }

  // Expires when the widget is destroyed: for whoever holds a Widget
  // pointer across a time the widget may go.
  std::weak_ptr<void> life() const { return alive_; }

  // Whether the keyboard focus may rest here (InputDispatcher).
  void setFocusable(bool focusable) { focusable_ = focusable; }
  bool focusable() const { return focusable_; }

  // The loop clock the contexts of this tree's events carry; set on the
  // root. Defaults to seconds on a steady clock.
  void setEventClock(std::function<double()> clock) { clock_ = std::move(clock); }

  // Offers a key or mouse event to this widget; true when it was used. The
  // default passes it on to the plane.
  virtual bool offerInput(const Input & input) {
    return plane_ ? plane_->offerInput(input) : false;
  }

  // Emacs-style named-command dispatch: subclasses populate keymap_/
  // commands_ (typically in their constructor) and call dispatchCommand()
  // from their own offerInput() wherever a migrated binding should take
  // effect. executeCommand() is the public entry point used to invoke a
  // command directly by name (e.g. from an command palette).
  bool executeCommand(std::string_view name) { return commands_.execute(std::string(name)); }

  // Read-only counterpart to executeCommand() - every name in this
  // element's own registry starting with `prefix` (autocomplete support
  // for a command prompt).
  std::vector<std::string> commandNames(std::string_view prefix) const {
    return commands_.matching(std::string(prefix));
  }

  Widget & putstr(int y, int x, const std::string & s) {
    if (plane_) plane_->putstr(y, x, s);
    return *this;
  }
  Widget & putstr(int y, int x, char c) {
    if (plane_) {
      std::string s;
      s += c;
      plane_->putstr(y, x, s);
    }
    return *this;
  }
#if 0
  Widget & putstrN(int y, int x, const std::string & s, int limit) {
    
  }
#endif
  Widget & move(int y, int x) {
    if (plane_) plane_->move(y, x);
    return *this;
  }
  // See Plane::moveToTop()'s own comment - raises this widget's plane
  // above every sibling under the same parent.
  Widget & moveToTop() {
    if (plane_) plane_->moveToTop();
    return *this;
  }
  Widget & setVisible(bool visible) {
    if (plane_) plane_->setVisible(visible);
    return *this;
  }
  Widget & resize(int rows, int cols) {
    if (plane_) plane_->resize(rows, cols);
    onResize();
    return *this;
  }
  Widget & erase() {
    if (plane_) plane_->erase();
    return *this;
  }
  Widget & fill() {
    if (plane_) {
      auto [rows, cols] = getDim();
      std::string s(static_cast<size_t>(cols), ' ');
      for (auto i = 0; i < rows; i++) plane_->putstr(i, 0, s);
    }
    return *this;
  }
  Widget & setFgColor(int r, int g, int b) { return setFgColor(Color(r, g, b)); }
  Widget & setBgColor(int r, int g, int b) { return setBgColor(Color(r, g, b)); }
  Widget & setFgColor(const Color & color) {
    if (plane_) plane_->setFgColor(color);
    return *this;
  }
  Widget & setBgColor(const Color & color) {
    if (plane_) plane_->setBgColor(color);
    return *this;
  }
  Widget & setBgDefault() {
    if (plane_) plane_->setBgDefault();
    return *this;
  }
  Widget & setUnderline(bool b) {
    if (plane_) plane_->setUnderline(b);
    return *this;
  }
  Widget & setBold(bool b) {
    if (plane_) plane_->setBold(b);
    return *this;
  }
  Widget & setItalic(bool b) {
    if (plane_) plane_->setItalic(b);
    return *this;
  }
  Widget & setLift(float lift, bool joins_above = false, bool casts_shadow = true) {
    if (plane_) plane_->setLift(lift, joins_above, casts_shadow);
    return *this;
  }

  std::pair<int, int> getPosition() const {
    if (plane_) return plane_->getPosition();
    else return std::pair(0, 0);
  }
  
  std::pair<int, int> getDim() const {
    if (plane_) return plane_->getDim();
    else return std::pair(0, 0);
  }
  
protected:
  void setPlane(std::unique_ptr<Plane> plane) {
    if (plane_) plane_->setOwner(nullptr);
    plane_ = std::move(plane);
    if (plane_) plane_->setOwner(this);
  }
  Plane & getPlane() { return *plane_; }
  const Plane & getPlane() const { return *plane_; }
  bool hasPlane() const { return plane_ != nullptr; }

  // Called after every resize() (including the initial construction-time
  // resize/move calls), so subclasses that cache geometry-
  // dependent state (e.g. an image sized to the plane) can invalidate it.
  // `resize()` is not virtual (it's called through base-typed handles), so
  // this hook exists to still reach subclass-specific behavior on resize.
  virtual void onResize() { }

public:
  // How `command` is bound in this widget's keymap on the display's profile
  // ("C-x C-s", "Ctrl+S"), the simplest of its bindings; "" if it has none.
  std::string bindingFor(const std::string & command) const {
    auto profile = keymapProfile();
    std::vector<uint64_t> best;
    auto simpler = [](const std::vector<uint64_t> & a, const std::vector<uint64_t> & b) {
      auto mods = [](const std::vector<uint64_t> & seq) {
        int n = 0;
        for (auto chord : seq) n += (chord & 1u ? 1 : 0) + (chord & 2u ? 1 : 0) + (chord & 4u ? 1 : 0) + (chord & 8u ? 1 : 0);
        return n;
      };
      if (a.size() != b.size()) return a.size() < b.size();
      if (mods(a) != mods(b)) return mods(a) < mods(b);
      return a < b;
    };
    for (auto & seq : keymap_.sequencesFor(command, profile)) {
      // A raw control byte (a terminal's own spelling of a chord) reads as
      // nothing; the chord it stands for is bound too.
      bool printable = true;
      for (auto chord : seq) {
        int id = KeyChord::idOf(chord);
        if (id < 0x20 && id != Key::Tab && id != Key::Escape) printable = false;
      }
      if (printable && (best.empty() || simpler(seq, best))) best = seq;
    }
    return best.empty() ? std::string() : KeyChord::describe(best, profile);
  }

  // Whether this widget wants a bare Escape keypress delivered right away
  // (e.g. to close a popup), rather than only once the next key shows
  // whether it was the start of an Alt chord.
  virtual bool wantsBareEscape() const { return false; }

protected:

  bool dispatchCommand(const Input & input) {
    // RELEASE now reaches offerInput() (see Input::Kind's own doc
    // comment) - no keymap-bound command is meant to fire on key-up, so
    // ignore it here rather than have every widget's keymap guard against
    // it individually. REPEAT is untouched (still actionable, same as
    // before) - e.g. holding a transpose/kill-region binding should keep
    // repeating, unlike a widget's own raw note-entry keys, which
    // suppress repeat themselves.
    if (input.getKind() == Input::Kind::RELEASE) return false;
    auto chord = KeyChord::pack(input);

    // Two-key Emacs-style prefix sequences (C-x C-s, C-x C-c, ...) - a
    // pending prefix always consumes the very next keystroke one way or
    // another, matching Emacs's own "C-x <undefined-key>" behavior of
    // aborting the sequence rather than letting the second key fall
    // through to whatever it would otherwise have done (e.g. self-insert
    // in a text field) - a half-typed prefix should never silently do
    // something else instead.
    auto profile = keymapProfile();
    if (pending_prefix_) {
      auto prefix = *pending_prefix_;
      pending_prefix_.reset();
      if (auto name = keymap_.lookupPrefixed(prefix, chord, profile)) return commands_.execute(*name);
      return true;
    }
    if (keymap_.isPrefix(chord, profile)) {
      pending_prefix_ = chord;
      return true;
    }

    if (auto name = keymap_.lookup(chord, profile)) return commands_.execute(*name);
    return false;
  }

  // The profile the display uses; bindings are looked up in it.
  KeymapProfile keymapProfile() const { return plane_ ? plane_->keymapProfile() : KeymapProfile::TERMINAL; }

  // Call after populating keymap_/commands_ in a subclass constructor to
  // catch a typo'd command name immediately instead of as a silently dead
  // keybinding.
  void assertCommandBindingsValid() const { assert(keymap_.allBoundIn(commands_)); }

  Keymap keymap_;
  CommandRegistry commands_;

  // Set by dispatchCommand() when the previous keystroke was a bound
  // prefix chord (e.g. C-x) - unset (no pending sequence) the rest of the
  // time. Per-instance, not shared - each widget that adopts prefix
  // bindings tracks its own in-progress sequence independently.
  std::optional<uint64_t> pending_prefix_;

  // Lets a widget with planes of its own draw on each through the ordinary
  // drawing helpers: exchanges the plane the helpers draw on with `other`.
  void swapPlane(std::unique_ptr<Plane> & other) {
    plane_.swap(other);
    if (plane_) plane_->setOwner(this);
  }

private:
  // The widgets of a tree that have a handler for each event type, kept on
  // the root so a root post visits the handlers, not the tree. Entries are
  // added when a handler is installed or a subtree is attached, and dropped
  // when found stale (the widget gone, moved to another tree, or with no
  // handler of that type left); each list is put in tree order only when the
  // tree changed since it last was.
  struct Registry {
    struct Entry {
      std::vector<std::shared_ptr<detail::HandlerTable>> tables;
      uint64_t sorted_epoch = ~uint64_t(0);
    };
    std::unordered_map<TypeKey, Entry> by_type;
    std::unordered_map<const detail::HandlerTable *, size_t> order;
    uint64_t order_epoch = ~uint64_t(0);

    void add(TypeKey type, const std::shared_ptr<detail::HandlerTable> & table) {
      auto & entry = by_type[type];
      if (std::find(entry.tables.begin(), entry.tables.end(), table) != entry.tables.end()) return;
      entry.tables.push_back(table);
      entry.sorted_epoch = ~uint64_t(0);
    }
  };

  Registry & registry() {
    if (!registry_) registry_ = std::make_unique<Registry>();
    return *registry_;
  }

  static void collectInterest(Widget & widget, Registry & registry) {
    for (TypeKey type : widget.table_->types()) registry.add(type, widget.table_);
    for (Widget * child : widget.children_) collectInterest(*child, registry);
  }
  // After a subtree moved: its handlers are the new root's to visit.
  static void registerSubtree(Widget & widget) { collectInterest(widget, widget.root().registry()); }

  static void number(Widget & widget, size_t & next, std::unordered_map<const detail::HandlerTable *, size_t> & out) {
    out[widget.table_.get()] = next++;
    for (Widget * child : widget.children_) number(*child, next, out);
  }

  // The handler tables of this root's tree for `type`, in tree order.
  std::vector<std::shared_ptr<detail::HandlerTable>> handlersInOrder(TypeKey type) {
    Registry & reg = registry();
    auto found = reg.by_type.find(type);
    if (found == reg.by_type.end()) return {};
    auto & entry = found->second;
    entry.tables.erase(std::remove_if(entry.tables.begin(), entry.tables.end(),
                                      [&](const std::shared_ptr<detail::HandlerTable> & table) {
                                        return !table->widget || &table->widget->root() != this || !table->has(type);
                                      }),
                       entry.tables.end());
    if (entry.sorted_epoch != epoch_) {
      if (reg.order_epoch != epoch_) {
        reg.order.clear();
        size_t next = 0;
        number(*this, next, reg.order);
        reg.order_epoch = epoch_;
      }
      std::sort(entry.tables.begin(), entry.tables.end(), [&](const std::shared_ptr<detail::HandlerTable> & a, const std::shared_ptr<detail::HandlerTable> & b) {
        return reg.order.at(a.get()) < reg.order.at(b.get());
      });
      entry.sorted_epoch = epoch_;
    }
    return entry.tables;
  }

  double now() {
    Widget & top = root();
    if (top.clock_) return top.clock_();
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  Handled dispatchTargeted(TypeKey type, const void * event) {
    // Snapshots, so a handler may add or remove handlers and widgets.
    std::vector<std::shared_ptr<detail::HandlerTable>> path;
    for (Widget * widget = this; widget; widget = widget->parent_) path.push_back(widget->table_);
    EventContext ctx;
    ctx.source = this;
    ctx.time = now();
    ctx.phase = Phase::capture;
    for (auto table = path.rbegin(); table != path.rend(); ++table) {
      if ((*table)->widget && (*table)->run(type, Phase::capture, event, ctx) == Handled::yes) {
        ctx.accepted = true;
        return Handled::yes;
      }
    }
    ctx.phase = Phase::bubble;
    for (auto & table : path) {
      if (table->widget && table->run(type, Phase::bubble, event, ctx) == Handled::yes) {
        ctx.accepted = true;
        return Handled::yes;
      }
    }
    return Handled::no;
  }

  Handled dispatchRoot(TypeKey type, const void * event) {
    auto tables = handlersInOrder(type);
    EventContext ctx;
    ctx.time = now();
    for (auto & table : tables) {
      if (table->widget && table->run(type, std::nullopt, event, ctx) == Handled::yes) {
        ctx.accepted = true;
        return Handled::yes;
      }
    }
    return Handled::no;
  }

  std::unique_ptr<Plane> plane_;
  std::shared_ptr<void> alive_ = std::make_shared<int>(0);
  bool focusable_ = false;
  std::shared_ptr<detail::HandlerTable> table_ = std::make_shared<detail::HandlerTable>();
  Widget * parent_ = nullptr;
  std::vector<Widget *> children_;
  // On a root: bumped whenever its tree changes shape.
  uint64_t epoch_ = 0;
  std::unique_ptr<Registry> registry_;
  std::function<double()> clock_;
};

}  // namespace cellui
