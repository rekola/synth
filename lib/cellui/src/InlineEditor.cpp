#include <cellui/InlineEditor.h>
#include <cellui/Plane.h>
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
  replaceable_ = field_.replace_on_type && !field_.initial_text.empty();
  plane_.showReader("", field_.row, field_.col, 1, field_.width, field_.initial_text, field_.text_color);
  plane_.setReaderBackdrop(field_.backdrop);
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
InlineEditor::key(const KeyDown & e) {
  if (!open_) return false;
  if (e.key == Key::Enter) {
    auto text = plane_.closeReader();
    auto commit = std::move(commit_);
    close();
    if (commit) commit(std::move(text));
  } else if (e.key == Key::Escape || (e.mods.ctrl && e.key == 'g')) {
    cancel();
  } else {
    if (replaceable_) {
      replaceable_ = false;
      bool printable = e.key >= 32 && e.key < 127 && !e.mods.ctrl && !e.mods.alt;
      if (printable || e.key == Key::Backspace || e.key == Key::Delete) plane_.setReaderContents("");
    }
    plane_.offerKey(e);
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
