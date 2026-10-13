#include <cellui/EscapeCoalescer.h>

namespace cellui {

EscapeSequenceCoalescer::EscapeSequenceCoalescer(InputEventSource & source) : source_(source) { }

void
EscapeSequenceCoalescer::setInPaste(bool in_paste) {
  in_paste_ = in_paste;
  if (in_paste_) {
    pending_ = false; // a paste's first character isn't retroactively Alt-chorded
    folded_id_ = -1;
  }
}

namespace {

bool
isMouse(int id) {
  return id >= NCKEY_MOTION && id <= NCKEY_BUTTON11;
}

// '[' (CSI) and 'O' (SS3) are the two legacy escape-sequence introducer
// bytes: a real Alt-[ or Alt-O is ambiguous with them at the byte level, so
// they are never folded.
bool
isSequenceIntroducer(const Input & in) {
  return in.getId() == '[' || in.getId() == 'O';
}

// A keystroke that already carries Ctrl, Shift or Meta is never folded: with
// no deadline, an incidental Escape followed by ordinary continued use
// (Ctrl-G, a Shift-modified note starting a chord column) is a real
// scenario, and nothing binds a Ctrl+Alt or Shift+Alt chord.
bool
isAlreadyModified(const Input & in) {
  return in.hasCtrl() || in.hasShift() || in.hasMeta();
}

// A mouse event or a resize says nothing about whether a pending Escape's
// wait should end.
bool
isKeyboardEvent(const Input & in) {
  if (in.getKind() == Input::Kind::RELEASE) return false;
  if (isMouse(in.getId())) return false;
  if (in.getId() == NCKEY_RESIZE) return false;
  return true;
}

bool
isUnmodifiedEscapePress(const Input & in) {
  return in.getId() == NCKEY_ESC && in.getKind() != Input::Kind::RELEASE && in.getKind() != Input::Kind::REPEAT && !in.hasAlt();
}

Input
withAlt(const Input & in) {
  return Input(in.getId(), in.getY(), in.getX(), true, in.hasShift(), in.hasCtrl(), in.hasMeta(), in.getKind(), in.getTrueCaseId());
}

}

bool
EscapeSequenceCoalescer::next(Input * input) {
  last_folded_ = false;
  if (!source_.next(input)) return false;

  if (in_paste_) {
    pending_ = false;
    return true;
  }

  // The release of a key whose press was folded is part of the same chord.
  if (input->getKind() == Input::Kind::RELEASE && input->getId() == folded_id_) {
    folded_id_ = -1;
    if (!input->hasAlt()) *input = withAlt(*input);
    return true;
  }

  if (pending_) {
    if (!isKeyboardEvent(*input)) return true; // noise - keep waiting
    pending_ = false;
    if (isUnmodifiedEscapePress(*input)) {
      pending_ = true; // another bare Escape just restarts the wait
    } else if (!isSequenceIntroducer(*input) && !isAlreadyModified(*input)) {
      if (!input->hasAlt()) *input = withAlt(*input);
      last_folded_ = true;
      folded_id_ = input->getId();
    }
    // else: declined, passed through as-is, and (like any other real
    // keyboard event) ends the wait.
    return true;
  }

  if (isUnmodifiedEscapePress(*input)) pending_ = true;
  return true;
}

}  // namespace cellui
