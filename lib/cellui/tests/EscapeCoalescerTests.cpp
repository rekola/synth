#include "TestFramework.h"

#include <cellui/EscapeCoalescer.h>

#include <deque>

using namespace std;
using namespace cellui;

namespace {

constexpr unsigned kShift = 1, kCtrl = 4;

// Legacy terminals report every keystroke's kind as UNKNOWN - that's the
// default here, so a test opts in to PRESS/RELEASE to simulate a terminal
// running the kitty keyboard protocol.
Input makeEvent(int id, Input::Kind kind = Input::Kind::UNKNOWN, unsigned modifiers = 0) {
  return Input(id, -1, -1, false, modifiers & kShift, modifiers & kCtrl, false, kind);
}

// A scripted, nonblocking InputEventSource - events become available in the
// order they're scheduled, with no notion of timing at all.
class FakeInputEventSource : public InputEventSource {
public:
  void schedule(Input event) { queue_.push_back(event); }

  bool next(Input * in) override {
    if (queue_.empty()) return false;
    *in = queue_.front();
    queue_.pop_front();
    return true;
  }

private:
  deque<Input> queue_;
};

struct Fixture {
  FakeInputEventSource source;
  EscapeSequenceCoalescer coalescer{source};
};

}

TEST(escape_alone_stays_pending_indefinitely) {
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));

  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Escape);
  CHECK(!ni.hasAlt());
  CHECK(f.coalescer.escapePending());

  // Nothing else scheduled - next() just reports "no event right now",
  // for as many calls as the caller cares to make; the wait itself never
  // expires on its own.
  CHECK(!f.coalescer.next(&ni));
  CHECK(!f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());
}

TEST(escape_then_w_much_later_still_coalesces_to_alt_w) {
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));

  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Escape);
  CHECK(f.coalescer.escapePending());

  // Simulates however long a real person takes to then press 'w' -
  // several intervening polls that find nothing.
  CHECK(!f.coalescer.next(&ni));
  CHECK(!f.coalescer.next(&ni));
  CHECK(!f.coalescer.next(&ni));

  f.source.schedule(makeEvent('w'));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'w');
  CHECK(ni.hasAlt());
  CHECK(!f.coalescer.escapePending());
}

TEST(mouse_and_resize_events_while_pending_do_not_end_the_wait) {
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));
  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent(cellui::Key::Button1, Input::Kind::PRESS));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Button1);
  CHECK(!ni.hasAlt());
  CHECK(f.coalescer.escapePending()); // still waiting - a click isn't a keyboard follow-up

  f.source.schedule(makeEvent(cellui::Key::Resize));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Resize);
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent('q'));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'q');
  CHECK(ni.hasAlt());
  CHECK(!f.coalescer.escapePending());
}

TEST(escape_then_bracket_or_O_declined_but_ends_the_wait) {
  // '[' and 'O' are legacy CSI/SS3 introducer bytes - a real Alt-[ or
  // Alt-O keypress is ambiguous with them at the byte level, so they're
  // never folded (see TerminalUI.cpp's own SS3-sequence recognizer,
  // which depends on seeing a plain, un-Alt-modified 'O' after ESC).
  for (int id : {'[', 'O'}) {
    Fixture f;
    f.source.schedule(makeEvent(cellui::Key::Escape));
    Input ni(0, 0, 0, false, false, false, false);
    CHECK(f.coalescer.next(&ni));
    CHECK(f.coalescer.escapePending());

    f.source.schedule(makeEvent(id));
    CHECK(f.coalescer.next(&ni));
    CHECK(ni.getId() == id);
    CHECK(!ni.hasAlt());
    CHECK(!f.coalescer.escapePending()); // a real keystroke was declined, not ignored - wait is over
  }
}

TEST(ctrl_modified_keystroke_after_pending_escape_is_not_folded) {
  // An incidental Escape (a habit left over from elsewhere, changing
  // one's mind mid-command, ...) followed by ordinary continued use -
  // Ctrl-G's own keyboard-quit included - must not have Alt silently
  // tacked onto it: nothing in this codebase binds a genuine Ctrl+Alt
  // chord, so folding here would just make Ctrl-G (and every other
  // Ctrl-modified binding) stop working until some later unmodified
  // keypress happened to absorb the fold instead.
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));
  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent('g', Input::Kind::UNKNOWN, kCtrl));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'g');
  CHECK(ni.hasCtrl());
  CHECK(!ni.hasAlt());
  CHECK(!f.coalescer.escapePending());
}

TEST(shift_modified_keystroke_after_pending_escape_is_not_folded) {
  // Same reasoning as the Ctrl case above: raw note entry uses a bare
  // Shift+letter to start a new chord column (PatternEditor.cpp) - Alt
  // arriving alongside it unasked would divert into the Alt-Left/Right
  // track-move branch instead and silently drop the note.
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));
  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent('w', Input::Kind::UNKNOWN, kShift));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'w');
  CHECK(ni.hasShift());
  CHECK(!ni.hasAlt());
  CHECK(!f.coalescer.escapePending());
}

TEST(a_second_bare_escape_restarts_the_wait) {
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));
  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent(cellui::Key::Escape));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Escape);
  CHECK(!ni.hasAlt());
  CHECK(f.coalescer.escapePending()); // still (freshly) waiting, not folded as Alt-Escape

  f.source.schedule(makeEvent('w'));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'w');
  CHECK(ni.hasAlt());
}

TEST(paste_containing_escape_not_coalesced) {
  Fixture f;
  f.coalescer.setInPaste(true);
  f.source.schedule(makeEvent(cellui::Key::Escape));
  f.source.schedule(makeEvent('w'));

  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Escape);
  CHECK(!f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'w');
  CHECK(!ni.hasAlt());
}

TEST(entering_paste_mid_wait_cancels_a_pending_escape) {
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Escape));
  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.coalescer.setInPaste(true);
  CHECK(!f.coalescer.escapePending());
}

TEST(kitty_protocol_escape_then_key_folds_across_the_releases) {
  Fixture f;
  // A kitty-protocol terminal reports every press and release: Escape's
  // release leaves the wait open, and the folded key's release is Alt too.
  f.source.schedule(makeEvent(cellui::Key::Escape, Input::Kind::PRESS));
  f.source.schedule(makeEvent(cellui::Key::Escape, Input::Kind::RELEASE));
  f.source.schedule(makeEvent('x', Input::Kind::PRESS));
  f.source.schedule(makeEvent('x', Input::Kind::RELEASE));
  f.source.schedule(makeEvent('y', Input::Kind::PRESS));

  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Escape && f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getKind() == Input::Kind::RELEASE && f.coalescer.escapePending() && !f.coalescer.lastFolded());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'x' && ni.hasAlt() && ni.getKind() == Input::Kind::PRESS && f.coalescer.lastFolded());
  CHECK(!f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'x' && ni.hasAlt() && ni.getKind() == Input::Kind::RELEASE && !f.coalescer.lastFolded());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'y' && !ni.hasAlt());
}

TEST(mouse_click_before_first_keystroke_does_not_disable_coalescing) {
  Fixture f;
  f.source.schedule(makeEvent(cellui::Key::Button1, Input::Kind::PRESS));

  Input ni(0, 0, 0, false, false, false, false);
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == cellui::Key::Button1);

  f.source.schedule(makeEvent(cellui::Key::Escape));
  f.source.schedule(makeEvent('w'));

  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.getId() == 'w');
  CHECK(ni.hasAlt());
}
