#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/ChoiceModal.h>
#include <cellui/InfoModal.h>
#include <cellui/InputDispatcher.h>
#include <cellui/PlatformEventLoop.h>

using namespace cellui;

namespace {

Input key(int id) { return Input(id, 0, 0, false, false, false, false, Input::Kind::PRESS); }

// A widget tree under a screen, and a loop that turns without waiting; `script`
// feeds the dialog one input a turn.
struct Fixture {
  StyleProvider styles;
  CellBuffer buffer{12, 40};
  BufferPlane screen{buffer, styles};
  Widget root;
  InputDispatcher dispatcher{root};
  PlatformEventLoop loop;
  int turn = 0;
  int widget_keys = 0;
  Connection widget_connection;
  std::vector<Input> script;
  Connection frame;

  Fixture() {
    loop.setBefore([] { return 0; });
    // A key the widgets would get if a dialog did not take it.
    widget_connection = root.on<KeyDown>([this](const KeyDown &, EventContext &) {
      widget_keys++;
      return Handled::no;
    });
    frame = loop.onFrame([this] {
      if (turn < static_cast<int>(script.size())) dispatcher.offer(script[static_cast<size_t>(turn++)]);
      else loop.quit();
    });
  }
};

}  // namespace

TEST(choicemodal_exec_returns_the_chosen_index) {
  Fixture f;
  f.script = { key(Key::Down), key(Key::Enter) };
  ChoiceModal dialog(f.root, f.screen, f.loop, "Pick", { "alpha", "beta", "gamma" }, 0);
  CHECK(dialog.showModal() == 1);
  CHECK(f.root.activeModal() == nullptr);
}

TEST(choicemodal_escape_returns_closed) {
  Fixture f;
  f.script = { key(Key::Escape) };
  ChoiceModal dialog(f.root, f.screen, f.loop, "Pick", { "alpha", "beta" }, 0);
  CHECK(dialog.showModal() == ModalDialog::kClosed);
}

TEST(modaldialog_has_every_key_while_up_and_its_scrim_goes_with_it) {
  Fixture f;
  bool scrim_while_up = false;
  f.script = { key('x'), key('y') };
  auto spy = f.loop.onFrame([&] {
    f.screen.refresh();
    for (auto & plane : f.screen.describe().children)
      if (plane.name == "scrim") scrim_while_up = true;
  });
  ChoiceModal dialog(f.root, f.screen, f.loop, "Pick", { "alpha" }, 0);
  CHECK(dialog.showModal() == ModalDialog::kClosed); // the loop was quit under it
  CHECK(f.widget_keys == 0);
  CHECK(scrim_while_up || !f.styles.theme.modal_backdrop.active());
  f.screen.refresh();
  for (auto & plane : f.screen.describe().children) CHECK(plane.name != "scrim");
}

TEST(infomodal_closes_on_escape_and_ignores_releases) {
  Fixture f;
  f.script = { Input(Key::Enter, 0, 0, false, false, false, false, Input::Kind::RELEASE), key(Key::Escape) };
  InfoModal dialog(f.root, f.screen, f.loop, "About", "text");
  CHECK(dialog.wantsNativeSelection());
  CHECK(dialog.showModal() == 0);
  CHECK(f.turn == 2);
}
