#include <cellui/InlineEditor.h>
#include <cellui/UIPlane.h>
#include <cellui/Input.h>

#include <utility>

namespace cellui {

bool
InlineEditor::open(const Field & field, std::function<void(std::string)> commit) {
  if (plane_.readerActive()) return false;
  field_ = field;
  if (field_.width < 1) field_.width = 1;
  commit_ = std::move(commit);
  open_ = true;
  plane_.showReader("", field_.row, field_.col, 1, field_.width, field_.initial_text, field_.text_color);
  paintBackdrop();
  // Opening erases from the field to the plane's right edge, which may
  // cover other content on the same row.
  redraw_requested_ = true;
  return true;
}

bool
InlineEditor::isOpen() const {
  return open_;
}

bool
InlineEditor::offerInput(const cellui::Input & input) {
  if (!open_) return false;
  // The release of the key that opened the field (Enter, in the pattern
  // editor) must not close it again.
  if (input.getKind() == cellui::Input::Kind::RELEASE) return true;
  if (input.getId() == Key::Enter) {
    auto text = plane_.closeReader();
    auto commit = std::move(commit_);
    close();
    if (commit) commit(std::move(text));
  } else if (input.hasCtrl() && input.getId() == 'g') {
    cancel();
  } else {
    plane_.offerInput(input);
  }
  return true;
}

void
InlineEditor::cancel() {
  if (!open_) return;
  plane_.closeReader();
  close();
}

void
InlineEditor::close() {
  open_ = false;
  commit_ = nullptr;
  redraw_requested_ = true;
}

void
InlineEditor::paintBackdrop() {
  if (!open_) return;
  auto text_color = field_.text_color.value_or(plane_.getStyles().reader_text_color);
  plane_.setFgColor(text_color);
  plane_.setBgColor(field_.backdrop);
  plane_.putstr(field_.row, field_.col, std::string(static_cast<size_t>(field_.width), ' '));
}

}  // namespace cellui
