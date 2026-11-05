#pragma once

#include <cellui/ChoiceDialog.h>
#include <cellui/ModalDialog.h>

#include <string>
#include <vector>

namespace cellui {

// A modal list to pick one entry from. showModal() returns the index chosen, or
// kClosed (-1) when it was cancelled.
class ChoiceModal : public ModalDialog {
 public:
  // `current` is marked and where the selection starts, -1 for none.
  ChoiceModal(Widget & root, Plane & screen, EventLoop & loop, std::string title, std::vector<std::string> labels, int current);

  bool animating() const override { return dialog_.animating(); }

 protected:
  void layout(Plane & screen, int rows, int cols) override;
  void keyDown(const KeyDown & e) override;
  void mouseUp(const MouseUp & e) override;
  void wheel(const Wheel & e) override;
  void close() override { dialog_.close(); }

 private:
  void handle(ChoiceDialog::Result result);
  ChoiceDialog dialog_;
};

}  // namespace cellui
