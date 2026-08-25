#include "TestFramework.h"

#include "../src/ui/EscapeCoalescer.h"

#include <deque>

using namespace std;

namespace {

// Legacy terminals report every keystroke's evtype as NCTYPE_UNKNOWN (see
// KittyProtocolDetector's own comment) - that's the default here so a
// test has to opt in to NCTYPE_PRESS/etc. explicitly to simulate a
// Kitty-protocol terminal instead.
ncinput makeEvent(uint32_t id, ncintype_e evtype = NCTYPE_UNKNOWN, unsigned modifiers = 0) {
  ncinput ni{};
  ni.id = id;
  ni.evtype = evtype;
  ni.modifiers = modifiers;
  return ni;
}

// A scripted, nonblocking InputEventSource - events become available in
// the order they're scheduled, with no notion of timing at all (matching
// EscapeSequenceCoalescer's own event-driven, no-deadline design: a test
// exercising the "waits indefinitely" behavior does so simply by calling
// next() an arbitrary number of times with nothing scheduled in between,
// not by advancing any clock).
class FakeInputEventSource : public InputEventSource {
public:
  void schedule(ncinput event) { queue_.push_back(event); }

  bool next(ncinput * ni) override {
    if (queue_.empty()) return false;
    *ni = queue_.front();
    queue_.pop_front();
    return true;
  }

private:
  deque<ncinput> queue_;
};

struct Fixture {
  FakeInputEventSource source;
  KittyProtocolDetector detector;
  EscapeSequenceCoalescer coalescer{source, detector};
};

}

TEST(escape_alone_stays_pending_indefinitely) {
  Fixture f;
  f.source.schedule(makeEvent(NCKEY_ESC));

  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_ESC);
  CHECK(!ncinput_alt_p(&ni));
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
  f.source.schedule(makeEvent(NCKEY_ESC));

  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_ESC);
  CHECK(f.coalescer.escapePending());

  // Simulates however long a real person takes to then press 'w' -
  // several intervening polls that find nothing.
  CHECK(!f.coalescer.next(&ni));
  CHECK(!f.coalescer.next(&ni));
  CHECK(!f.coalescer.next(&ni));

  f.source.schedule(makeEvent('w'));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(ncinput_alt_p(&ni));
  CHECK(!f.coalescer.escapePending());
}

TEST(mouse_and_resize_events_while_pending_do_not_end_the_wait) {
  Fixture f;
  f.source.schedule(makeEvent(NCKEY_ESC));
  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent(NCKEY_BUTTON1, NCTYPE_PRESS));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_BUTTON1);
  CHECK(!ncinput_alt_p(&ni));
  CHECK(f.coalescer.escapePending()); // still waiting - a click isn't a keyboard follow-up

  f.source.schedule(makeEvent(NCKEY_RESIZE));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_RESIZE);
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent('q'));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'q');
  CHECK(ncinput_alt_p(&ni));
  CHECK(!f.coalescer.escapePending());
}

TEST(escape_then_bracket_or_O_declined_but_ends_the_wait) {
  // '[' and 'O' are legacy CSI/SS3 introducer bytes - a real Alt-[ or
  // Alt-O keypress is ambiguous with them at the byte level, so they're
  // never folded (see TerminalUI.cpp's own SS3-sequence recognizer,
  // which depends on seeing a plain, un-Alt-modified 'O' after ESC).
  for (uint32_t id : {static_cast<uint32_t>('['), static_cast<uint32_t>('O')}) {
    Fixture f;
    f.source.schedule(makeEvent(NCKEY_ESC));
    ncinput ni;
    CHECK(f.coalescer.next(&ni));
    CHECK(f.coalescer.escapePending());

    f.source.schedule(makeEvent(id));
    CHECK(f.coalescer.next(&ni));
    CHECK(ni.id == id);
    CHECK(!ncinput_alt_p(&ni));
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
  f.source.schedule(makeEvent(NCKEY_ESC));
  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent('g', NCTYPE_UNKNOWN, NCKEY_MOD_CTRL));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'g');
  CHECK(ncinput_ctrl_p(&ni));
  CHECK(!ncinput_alt_p(&ni));
  CHECK(!f.coalescer.escapePending());
}

TEST(shift_modified_keystroke_after_pending_escape_is_not_folded) {
  // Same reasoning as the Ctrl case above: raw note entry uses a bare
  // Shift+letter to start a new chord column (PatternEditor.cpp) - Alt
  // arriving alongside it unasked would divert into the Alt-Left/Right
  // track-move branch instead and silently drop the note.
  Fixture f;
  f.source.schedule(makeEvent(NCKEY_ESC));
  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent('w', NCTYPE_UNKNOWN, NCKEY_MOD_SHIFT));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(ncinput_shift_p(&ni));
  CHECK(!ncinput_alt_p(&ni));
  CHECK(!f.coalescer.escapePending());
}

TEST(a_second_bare_escape_restarts_the_wait) {
  Fixture f;
  f.source.schedule(makeEvent(NCKEY_ESC));
  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.source.schedule(makeEvent(NCKEY_ESC));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_ESC);
  CHECK(!ncinput_alt_p(&ni));
  CHECK(f.coalescer.escapePending()); // still (freshly) waiting, not folded as Alt-Escape

  f.source.schedule(makeEvent('w'));
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(ncinput_alt_p(&ni));
}

TEST(paste_containing_escape_not_coalesced) {
  Fixture f;
  f.coalescer.setInPaste(true);
  f.source.schedule(makeEvent(NCKEY_ESC));
  f.source.schedule(makeEvent('w'));

  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_ESC);
  CHECK(!f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(!ncinput_alt_p(&ni));
}

TEST(entering_paste_mid_wait_cancels_a_pending_escape) {
  Fixture f;
  f.source.schedule(makeEvent(NCKEY_ESC));
  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());

  f.coalescer.setInPaste(true);
  CHECK(!f.coalescer.escapePending());
}

TEST(kitty_protocol_active_passes_events_through_untouched) {
  Fixture f;
  f.detector.observe(NCTYPE_PRESS); // settles the detector active, as a real Kitty terminal's first keystroke would
  CHECK(f.detector.active());

  f.source.schedule(makeEvent(NCKEY_ESC, NCTYPE_PRESS));
  f.source.schedule(makeEvent('w', NCTYPE_PRESS));

  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_ESC);
  CHECK(!f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(!ncinput_alt_p(&ni));
}

TEST(notcurses_legacy_alt_merge_is_normalized_into_modifiers) {
  // notcurses's own walk_automaton() (automaton.c) already merges ESC
  // immediately followed by an unmatched single byte into one ncinput for
  // that byte - it never surfaces a separate ESC event for next() to
  // coalesce at all - but records the result only via the deprecated
  // `ncinput.alt` bool, never `modifiers`. This never reaches next() as
  // an ESC event; it arrives already merged, exactly like this.
  Fixture f;
  ncinput merged = makeEvent('w');
  merged.alt = true;
  f.source.schedule(merged);

  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(ncinput_alt_p(&ni));
  CHECK(!f.coalescer.escapePending());
}

TEST(mouse_click_before_first_keystroke_does_not_disable_coalescing) {
  Fixture f;
  // A mouse click's evtype is always real (SGR mouse reporting always
  // distinguishes press from release), even on a legacy terminal that
  // reports NCTYPE_UNKNOWN for every keyboard event - feeding it to the
  // detector must not be mistaken for Kitty keyboard protocol support,
  // or every keystroke after the session's first mouse click would stop
  // coalescing.
  f.source.schedule(makeEvent(NCKEY_BUTTON1, NCTYPE_PRESS));

  ncinput ni;
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == NCKEY_BUTTON1);
  CHECK(!f.detector.active());

  f.source.schedule(makeEvent(NCKEY_ESC));
  f.source.schedule(makeEvent('w'));

  CHECK(f.coalescer.next(&ni));
  CHECK(f.coalescer.escapePending());
  CHECK(f.coalescer.next(&ni));
  CHECK(ni.id == 'w');
  CHECK(ncinput_alt_p(&ni));
}

TEST(kitty_protocol_detector_latches_inactive_on_first_unknown) {
  KittyProtocolDetector d;
  CHECK(!d.active());
  d.observe(NCTYPE_UNKNOWN);
  CHECK(!d.active());
  d.observe(NCTYPE_PRESS); // already settled - must not flip
  CHECK(!d.active());
}

TEST(kitty_protocol_detector_latches_active_on_first_press) {
  KittyProtocolDetector d;
  d.observe(NCTYPE_PRESS);
  CHECK(d.active());
  d.observe(NCTYPE_UNKNOWN); // already settled - must not flip
  CHECK(d.active());
}
