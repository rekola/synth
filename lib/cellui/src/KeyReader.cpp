#include <cellui/KeyReader.h>

namespace cellui {

bool KeyReader::next(Input * input) {
  if (!escape_as_meta_) {
    // Escape is an ordinary key: delivered at once unless something took it.
    while (source_.next(input)) {
      bool bare_escape = input->getId() == Key::Escape && !input->hasAlt();
      if (bare_escape && input->getKind() == Input::Kind::RELEASE) {
        if (escape_taken_) {
          escape_taken_ = false;
          continue;
        }
      } else if (bare_escape && input->getKind() != Input::Kind::RELEASE) {
        escape_taken_ = takeEscape && takeEscape(*input);
        if (escape_taken_) continue;
      }
      return true;
    }
    return false;
  }
  while (out_.empty()) {
    Input in(0, -1, -1, false, false, false, false);
    bool was_pending = coalescer_.escapePending();
    if (!coalescer_.next(&in)) return false;
    bool now_pending = coalescer_.escapePending();
    if (now_pending && !was_pending) pending_since_ = Clock::now();
    else if (!now_pending) pending_since_.reset();

    // A release, a mouse event or a resize says nothing about the held
    // Escape; Escape's own release belongs to it.
    bool undeciding = in.getKind() == Input::Kind::RELEASE || in.getId() == Key::Resize ||
                      (in.getId() >= Key::Motion && in.getId() <= Key::Button11);
    if (held_ && undeciding) {
      if (in.getId() != Key::Escape) out_.push_back(in);
      continue;
    }
    if (held_) {
      // The held Escape goes ahead of what followed it, unless it was taken
      // or became that input's Alt prefix.
      if (!taken_ && !coalescer_.lastFolded()) out_.push_back(*held_);
      held_.reset();
      taken_ = false;
    }
    if (in.getId() == Key::Escape && !in.hasAlt() && in.getKind() != Input::Kind::RELEASE) {
      held_ = in;
      taken_ = takeEscape && takeEscape(in);
      continue;
    }
    out_.push_back(in);
  }
  *input = out_.front();
  out_.pop_front();
  return true;
}

}  // namespace cellui
