#include "TestFramework.h"

#include <cellui/Element.h>

#include <string>

using namespace cellui;

namespace {

// Records what is drawn; every other plane operation does nothing.
class RecordingPlane : public UIPlane {
 public:
  explicit RecordingPlane(const StyleProvider & styles) : UIPlane(nullptr, styles) { }

  std::string drawn;
  int fg_red = -1;

  void setFgColor(int r, int, int) override { fg_red = r; }
  void setBgColor(int, int, int) override { }
  void setUnderline(bool) override { }
  void setBold(bool) override { }
  void setItalic(bool) override { }
  void erase() override { drawn.clear(); }
  void putstr(int, int, const std::string & s) override { drawn += s; }
  std::unique_ptr<UIPlane> createChild() override { return std::make_unique<RecordingPlane>(getStyles()); }
  void drawBorder() override { }
  int offered = 0;
  bool offerInput(const Input &) override { offered++; return true; }
  void showReader(const std::string &, int, int, int, int, const std::string &, std::optional<Color>) override { }
  std::string closeReader() override { return ""; }
  bool readerActive() const override { return false; }
  std::string getReaderContents() const override { return ""; }
  void setReaderContents(const std::string &) override { }
  void showReaderIndicator(int, const std::string &) override { }
  void hideReaderIndicator() override { }
  void showPicker(int, int, int, int, int) override { }
  void addItem(const std::string &, const std::string &) override { }
  void selectPickerItem(int) override { }
  bool pickerActive() const override { return false; }
  std::string getPickerSelection() const override { return ""; }
  void closePicker() override { }
  void refresh() override { }
};

class Probe : public Element {
 public:
  explicit Probe(UIPlane & parent) : Element(parent) {
    commands_.define("hello", [this] { hello++; });
    commands_.define("prefixed", [this] { prefixed++; });
    keymap_.bind(KeyChord::pack('h', false, false, false, false), "hello");
    auto ctrl_x = KeyChord::pack('x', true, false, false, false);
    keymap_.bindPrefixed(ctrl_x, KeyChord::pack('p', false, false, false, false), "prefixed");
    assertCommandBindingsValid();
  }
  bool press(int id, bool ctrl = false) {
    return dispatchCommand(Input(id, 0, 0, false, false, ctrl, false, Input::Kind::PRESS));
  }
  bool release(int id) {
    return dispatchCommand(Input(id, 0, 0, false, false, false, false, Input::Kind::RELEASE));
  }
  void onResize() override { resized++; }
  int hello = 0, prefixed = 0, resized = 0;
};

}  // namespace

TEST(element_draws_through_its_plane_and_chains) {
  StyleProvider styles;
  RecordingPlane root(styles);
  Probe p(root);
  p.setFgColor(Color("#102030")).putstr(0, 0, "ab").putstr(0, 2, 'c');
  CHECK(p.getDim() == std::pair(0, 0));
  p.resize(3, 4);
  CHECK(p.getDim() == std::pair(3, 4));
  CHECK(p.resized == 1);
}

TEST(element_dispatches_bound_chords_but_not_releases) {
  StyleProvider styles;
  RecordingPlane root(styles);
  Probe p(root);
  CHECK(p.press('h'));
  CHECK(p.hello == 1);
  CHECK(!p.release('h'));
  CHECK(p.hello == 1);
  CHECK(!p.press('q'));
}

TEST(element_prefix_sequence_consumes_the_next_key) {
  StyleProvider styles;
  RecordingPlane root(styles);
  Probe p(root);
  CHECK(p.press('x', true));
  CHECK(p.press('p'));
  CHECK(p.prefixed == 1);
  // A prefix followed by an unbound key aborts the sequence and swallows the key.
  CHECK(p.press('x', true));
  CHECK(p.press('h'));
  CHECK(p.hello == 0);
  CHECK(p.press('h'));
  CHECK(p.hello == 1);
}

TEST(element_offers_input_to_its_plane_by_default) {
  StyleProvider styles;
  RecordingPlane root(styles);
  class Plain : public Element {
   public:
    explicit Plain(UIPlane & parent) : Element(parent) { }
    RecordingPlane & plane() { return static_cast<RecordingPlane &>(getPlane()); }
  } e(root);
  Input in('a', 0, 0, false, false, false, false);
  CHECK(e.offerInput(in));
  CHECK(e.plane().offered == 1);
  Element none;
  CHECK(!none.offerInput(in));
}
