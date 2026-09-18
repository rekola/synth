#include "InlineEditor.h"
#include "../UIPlane.h"
#include "../../playback/EventHandler.h"
#include "../../playback/InputEvent.h"

#include <utility>

bool
InlineEditor::open(const Field & field, std::function<void(std::string)> commit) {
  if (plane_.readerActive()) return false;
  field_ = field;
  if (field_.width < 1) field_.width = 1;
  commit_ = std::move(commit);
  open_ = true;
  plane_.showReader("", field_.row, field_.col, 1, field_.width, field_.initial_text,
                    field_.text_color.getRed(), field_.text_color.getGreen(), field_.text_color.getBlue());
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
InlineEditor::offerInput(const InputEvent & input) {
  if (!open_) return false;
  if (input.getId() == NCKEY_ENTER) {
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
  plane_.setFgColor(field_.text_color.getRed(), field_.text_color.getGreen(), field_.text_color.getBlue());
  plane_.setBgColor(field_.backdrop.getRed(), field_.backdrop.getGreen(), field_.backdrop.getBlue());
  plane_.putstr(field_.row, field_.col, std::string(static_cast<size_t>(field_.width), ' '));
}
