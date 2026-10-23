#pragma once

#include <cellui/Input.h>
#include <cellui/InputEvents.h>
#include <cellui/PlaneInfo.h>
#include <cellui/Widget.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cellui {

// Turns a display's input into events on one widget tree.
//
// - Keys go to the focused widget (the root when none is), as targeted
//   posts: capture from the root down, bubble back up.
// - A pointer event goes to the topmost widget under it, found in the hit
//   areas of the last frame (BufferPlane::takeHitAreas(), handed over with
//   setHitAreas()). The press that starts a gesture grabs the pointer: the
//   moves and the release go to the widget it hit until the last button is
//   up, wherever the pointer is.
// - Enter and Leave follow the widget under the pointer (or the grabbing
//   one); a press moves the focus to the nearest focusable widget it is on.
//
// Widgets may be destroyed at any time, in a handler included; the
// dispatcher keeps only pointers it can tell are alive. The root must
// outlive the dispatcher.
class InputDispatcher {
 public:
  explicit InputDispatcher(Widget & root) : root_(&root) { }

  void setHitAreas(std::vector<HitArea> areas) { areas_ = std::move(areas); }

  // The topmost widget of this tree at a cell; null where no plane of it
  // reaches.
  Widget * hitTest(int row, int col) const {
    for (auto area = areas_.rbegin(); area != areas_.rend(); ++area) {
      if (row < area->row || row >= area->row + area->rows || col < area->col || col >= area->col + area->cols) continue;
      if (area->life.expired() || !inTree(area->widget)) continue;
      return area->widget;
    }
    return nullptr;
  }

  // --- Focus

  Widget * focused() const { return focus_.get(); }

  // Moves the focus (null clears it): FocusOut to the old widget, FocusIn to
  // the new. A widget of another tree is ignored.
  void setFocus(Widget * widget) {
    if (widget && !inTree(widget)) return;
    Widget * old = focus_.get();
    if (old == widget) return;
    focus_.set(widget);
    if (old) old->postDirect(FocusOut{});
    if (Widget * now = focus_.get(); now && now == widget) now->postDirect(FocusIn{});
  }

  // The next or previous focusable widget in tree order, wrapping round.
  // False when there is none to move to.
  bool focusNext() { return moveFocus(1); }
  bool focusPrev() { return moveFocus(-1); }

  // --- Events

  Handled keyDown(const KeyDown & event) { return target(focus_.get()).post(event); }
  Handled keyUp(const KeyUp & event) { return target(focus_.get()).post(event); }

  Handled mouseDown(const MouseDown & event) {
    Widget * hit = hitTest(event.row, event.col);
    if (pressed_ == 0) grab_.set(hit);
    pressed_ |= bit(event.button);
    Widget * to = grab_.get() ? grab_.get() : hit;
    updateHover(to, event.row, event.col);
    if (hit == to) focusOn(to);
    return target(to).post(event);
  }

  Handled mouseMove(const MouseMove & event) {
    Widget * hit = hitTest(event.row, event.col);
    Widget * to = grab_.get() ? grab_.get() : hit;
    updateHover(to, event.row, event.col);
    return target(to).post(event);
  }

  Handled mouseUp(const MouseUp & event) {
    Widget * hit = hitTest(event.row, event.col);
    Widget * to = grab_.get() ? grab_.get() : hit;
    pressed_ &= ~bit(event.button);
    if (pressed_ == 0) grab_.set(nullptr);
    Handled handled = target(to).post(event);
    if (pressed_ == 0) updateHover(hit, event.row, event.col);
    return handled;
  }

  Handled wheel(const Wheel & event) { return target(hitTest(event.row, event.col)).post(event); }

  // The pointer left the display: whatever it was over is left.
  void pointerLeft() { updateHover(nullptr, 0, 0); }

  Handled resize(const Resize & event) { return root_->postToRoot(event); }

  // The held buttons, bit n-1 for button n.
  unsigned pressedButtons() const { return pressed_; }

  // Translates a cellui::Input (the terminal's and the window's form: Key::
  // ids for the pointer, positions in cells) and dispatches it.
  Handled offer(const Input & input) {
    int id = input.getId();
    Modifiers mods{ input.hasShift(), input.hasCtrl(), input.hasAlt(), input.hasMeta() };
    int row = input.getY(), col = input.getX();
    float x = input.getFx(), y = input.getFy();
    auto kind = input.getKind();
    if (id == Key::Motion) return mouseMove(MouseMove{ x, y, row, col, pressed_, mods });
    if (id >= Key::Button1 && id <= Key::Button11) {
      int button = id - Key::Button1 + 1;
      if (button <= 3) {
        if (kind == Input::Kind::RELEASE) return mouseUp(MouseUp{ x, y, row, col, button, mods });
        // A held button's press again is the pointer dragging.
        if (kind == Input::Kind::REPEAT) return mouseMove(MouseMove{ x, y, row, col, pressed_, mods });
        return mouseDown(MouseDown{ x, y, row, col, button, mods });
      }
      if (button <= 7 && kind != Input::Kind::RELEASE) {
        // Wheel: 4 up, 5 down, 6 left, 7 right.
        Wheel wheel{ x, y, row, col, button == 7 ? 1 : button == 6 ? -1 : 0, button == 4 ? 1 : button == 5 ? -1 : 0, mods };
        return this->wheel(wheel);
      }
      return Handled::no;
    }
    if (id == Key::Resize) return Handled::no;
    if (kind == Input::Kind::RELEASE) return keyUp(KeyUp{ id, mods });
    KeyDown down{ id, mods, "", kind == Input::Kind::REPEAT };
    int ch = input.getTrueCaseId();
    if (!mods.ctrl && !mods.alt && !mods.meta && ch >= 0x20 && ch != 0x7f && ch < Key::First) appendUtf8(down.text, ch);
    return keyDown(down);
  }

 private:
  // A widget pointer that knows when the widget is gone.
  struct Ref {
    Widget * widget = nullptr;
    std::weak_ptr<void> life;
    Widget * get() const { return widget && !life.expired() ? widget : nullptr; }
    void set(Widget * w) {
      widget = w;
      if (w) life = w->life();
      else life.reset();
    }
  };

  static unsigned bit(int button) { return button >= 1 && button <= 32 ? 1u << (button - 1) : 0u; }

  bool inTree(Widget * widget) const { return widget && &widget->root() == root_; }

  Widget & target(Widget * widget) { return widget && inTree(widget) ? *widget : *root_; }

  static void appendUtf8(std::string & out, int cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  // A press focuses the nearest focusable widget from the target up.
  void focusOn(Widget * widget) {
    for (; widget; widget = widget->parent()) {
      if (widget->focusable()) {
        setFocus(widget);
        return;
      }
    }
  }

  std::vector<Widget *> focusOrder() const {
    std::vector<Widget *> out;
    auto walk = [&](auto & self, Widget * widget) -> void {
      if (widget->focusable()) out.push_back(widget);
      for (Widget * child : widget->children()) self(self, child);
    };
    walk(walk, root_);
    return out;
  }

  bool moveFocus(int step) {
    std::vector<Widget *> order = focusOrder();
    if (order.empty()) return false;
    auto at = std::find(order.begin(), order.end(), focus_.get());
    size_t n = order.size();
    size_t next;
    if (at == order.end()) next = step > 0 ? 0 : n - 1;
    else next = (static_cast<size_t>(at - order.begin()) + (step > 0 ? 1 : n - 1)) % n;
    if (order[next] == focus_.get()) return false;
    setFocus(order[next]);
    return true;
  }

  // The widgets from the root down to `widget`.
  std::vector<Widget *> pathTo(Widget * widget) const {
    std::vector<Widget *> path;
    for (; widget; widget = widget->parent()) path.push_back(widget);
    std::reverse(path.begin(), path.end());
    return path;
  }

  // Leave for what the pointer was over and no longer is (deepest first),
  // Enter for what it is over now (outermost first). The tree may change in
  // a handler, so each widget is looked at again just before it is posted to.
  void updateHover(Widget * over, int row, int col) {
    std::vector<Ref> next;
    for (Widget * widget : pathTo(over && inTree(over) ? over : nullptr)) {
      Ref ref;
      ref.set(widget);
      next.push_back(ref);
    }
    std::vector<Ref> old = std::move(hover_);
    hover_ = next;
    size_t common = 0;
    while (common < old.size() && common < next.size() && old[common].get() && old[common].get() == next[common].get()) common++;
    for (size_t i = old.size(); i-- > common;) {
      if (Widget * widget = old[i].get()) widget->postDirect(Leave{ row, col });
    }
    for (size_t i = common; i < next.size(); i++) {
      if (Widget * widget = next[i].get()) widget->postDirect(Enter{ row, col });
    }
  }

  Widget * root_;
  std::vector<HitArea> areas_;
  Ref focus_;
  Ref grab_;
  unsigned pressed_ = 0;
  std::vector<Ref> hover_;
};

}  // namespace cellui
