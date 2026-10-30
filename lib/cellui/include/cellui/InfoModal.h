#pragma once

#include <cellui/InfoDialog.h>
#include <cellui/ModalDialog.h>

#include <string>

namespace cellui {

// A modal text box (Markdown, see InfoDialog) that Escape, Enter, q or Ctrl+G
// close. The display's own text selection works over it. showModal() returns 0.
class InfoModal : public ModalDialog {
 public:
  InfoModal(Widget & root, Plane & screen, EventLoop & loop, std::string title, std::string markdown);

  bool wantsNativeSelection() const override { return true; }

 protected:
  void layout(Plane & screen, int rows, int cols) override;
  void offerInput(const Input & input) override;
  void close() override { dialog_.close(); }

 private:
  InfoDialog dialog_;
  std::string title_, markdown_;
};

}  // namespace cellui
