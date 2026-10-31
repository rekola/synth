#include "TestFramework.h"

#include <cellui/term/InputParser.h>

#include <string>
#include <vector>

using namespace cellui;
using namespace cellui::term;

namespace {

using Ms = std::chrono::milliseconds;

// One read: the bytes, after a pause of `delay_ms` since the previous one.
struct Chunk {
  int delay_ms;
  std::string bytes;
};

// Feeds the chunks with a fake clock, letting deadlines expire in the
// pauses, then `trailing_ms` more; returns every event described.
std::vector<std::string> run(InputParser & p, const std::vector<Chunk> & chunks, int trailing_ms = 1000) {
  auto now = InputParser::TimePoint{};
  std::vector<std::string> out;
  auto drain = [&]() {
    while (p.hasEvents()) out.push_back(describe(p.pop()));
  };
  auto advance = [&](int ms) {
    auto until = now + Ms(ms);
    while (p.nextDeadline() && *p.nextDeadline() <= until) {
      now = *p.nextDeadline();
      p.expire(now);
      drain();
    }
    now = until;
  };
  for (const Chunk & c : chunks) {
    advance(c.delay_ms);
    p.feed(c.bytes, now);
    drain();
  }
  advance(trailing_ms);
  return out;
}

std::vector<std::string> run(const std::vector<Chunk> & chunks) {
  InputParser p;
  return run(p, chunks);
}

std::vector<std::string> run(const std::string & bytes) { return run({{0, bytes}}); }

std::vector<std::string> v(std::initializer_list<const char *> l) { return std::vector<std::string>(l.begin(), l.end()); }

bool same(const std::vector<std::string> & got, const std::vector<std::string> & want) {
  if (got == want) return true;
  std::fprintf(stderr, "    got:");
  for (auto & s : got) std::fprintf(stderr, " [%s]", s.c_str());
  std::fprintf(stderr, "\n    want:");
  for (auto & s : want) std::fprintf(stderr, " [%s]", s.c_str());
  std::fprintf(stderr, "\n");
  return false;
}

}  // namespace

TEST(parser_ground_controls) {
  CHECK(same(run("a"), v({"a"})));
  CHECK(same(run("\x01"), v({"Ctrl+A"})));
  CHECK(same(run("\x7f"), v({"Backspace"})));
  CHECK(same(run("\x08"), v({"Backspace"})));
  CHECK(same(run("\r"), v({"Enter"})));
  CHECK(same(run("\n"), v({"Enter"})));
  CHECK(same(run("\t"), v({"Tab"})));
  CHECK(same(run("\x1b[Z"), v({"Shift+Tab"})));
  CHECK(same(run(std::string(1, '\0')), v({"Ctrl+Space"})));
  CHECK(same(run("\x1f"), v({"Ctrl+_"})));
  CHECK(same(run("\x1c"), v({"Ctrl+\\"})));
  CHECK(same(run("\xc3\xa4"), v({"ä"})));
}

TEST(parser_lone_escape_waits_only_for_the_grace_period) {
  CHECK(same(run({{0, "\x1b"}, {1500, "x"}}), v({"Esc", "x"})));
  CHECK(same(run({{0, "\x1b"}, {10, "x"}}), v({"Alt+x"})));
  CHECK(same(run({{0, "\x1b"}, {1500, "\x1b"}}), v({"Esc", "Esc"})));
  // Nothing is held past the deadline, so nothing is left to wait for.
  InputParser p;
  p.feed("\x1b", InputParser::TimePoint{});
  CHECK(p.nextDeadline().has_value());
  CHECK(*p.nextDeadline() == InputParser::TimePoint{} + Ms(25));
  p.expire(InputParser::TimePoint{} + Ms(25));
  CHECK(!p.nextDeadline());
  CHECK(p.hasEvents() && describe(p.pop()) == "Esc");
}

TEST(parser_escape_prefix_is_alt) {
  CHECK(same(run("\x1bx"), v({"Alt+x"})));
  CHECK(same(run("\x1bX"), v({"Alt+X"})));
  CHECK(same(run("\x1b\x01"), v({"Ctrl+Alt+A"})));
  CHECK(same(run("\x1b\xc3\xa4"), v({"Alt+ä"})));
  CHECK(same(run("\x1b\x7f"), v({"Alt+Backspace"})));
}

TEST(parser_legacy_keys) {
  CHECK(same(run("\x1b[A"), v({"Up"})));
  CHECK(same(run("\x1bOA"), v({"Up"})));
  CHECK(same(run("\x1b[1;5A"), v({"Ctrl+Up"})));
  CHECK(same(run("\x1b[1;3D"), v({"Alt+Left"})));
  CHECK(same(run("\x1b[1;2H"), v({"Shift+Home"})));
  CHECK(same(run("\x1b[1~"), v({"Home"})));
  CHECK(same(run("\x1b[4~"), v({"End"})));
  CHECK(same(run("\x1b[7~"), v({"Home"})));
  CHECK(same(run("\x1bOP"), v({"F1"})));
  CHECK(same(run("\x1b[1;2P"), v({"Shift+F1"})));
  CHECK(same(run("\x1b[2P"), v({"Shift+F1"})));
  CHECK(same(run("\x1b[15;5~"), v({"Ctrl+F5"})));
  CHECK(same(run("\x1b[24~"), v({"F12"})));
  CHECK(same(run("\x1b[3;5~"), v({"Ctrl+Delete"})));
  CHECK(same(run("\x1b[1;5R"), v({"Ctrl+F3"})));
  CHECK(same(run("\x1bOM"), v({"Enter"})));
  CHECK(same(run("\x1bOq"), v({"1"})));
}

TEST(parser_keypad_operators_with_ctrl) {
  CHECK(same(run("\x1bO5o"), v({"Ctrl+KpDivide"})));
  CHECK(same(run("\x1bO5j"), v({"Ctrl+KpMultiply"})));
  CHECK(same(run("\x1bOo"), v({"/"})));
}

TEST(parser_modify_other_keys) {
  CHECK(same(run("\x1b[27;5;9~"), v({"Ctrl+Tab"})));
  CHECK(same(run("\x1b[27;3;97~"), v({"Alt+a"})));
  CHECK(same(run("\x1b[27;13;97~"), v({"Ctrl+Meta+A"})));
}

TEST(parser_utf8_split_across_reads) {
  CHECK(same(run({{0, "\xe7"}, {10, "\x95\x8c"}}), v({"界"})));
  CHECK(same(run({{0, "\xe7"}, {600, "\x95\x8c"}}), v({"\xef\xbf\xbd", "\xef\xbf\xbd", "\xef\xbf\xbd"})));
  CHECK(same(run({{0, "\xe7"}, {100, "a"}}), v({"\xef\xbf\xbd", "a"})));
  CHECK(same(run("\xe7" "a"), v({"\xef\xbf\xbd", "a"})));
}

TEST(parser_keys_around_a_sequence) {
  CHECK(same(run("ab\x1b[Acd"), v({"a", "b", "Up", "c", "d"})));
}

TEST(parser_bracketed_paste) {
  CHECK(same(run("\x1b[200~hi\x1b[201~"), v({"paste \"hi\""})));
  CHECK(same(run("\x1b[200~a\x1b[Ab\x1b[201~"), v({"paste \"a\x1b[Ab\""})));
  CHECK(same(run("\x1b[200~a\r\nb\rc\x1b[201~"), v({"paste \"a\nb\nc\""})));
  CHECK(same(run({{0, "\x1b[200~ab"}, {2000, "c\x1b[2"}, {2000, "01~x"}}), v({"paste \"abc\"", "x"})));
  CHECK(same(run("\x1b[200~\xff\x1b[201~"), v({"paste \"\xef\xbf\xbd\""})));
  InputParser p;
  p.setPasteCap(4);
  CHECK(same(run(p, {{0, "\x1b[200~abcdef\x1b[201~"}}), v({"paste \"abcd\" continues", "paste \"ef\""})));
}

TEST(parser_focus) {
  CHECK(same(run("\x1b[I\x1b[O"), v({"focus in", "focus out"})));
}

TEST(parser_replies_are_not_keys) {
  // Unsolicited cursor report with one parameter pair and a primary DA.
  InputParser p;
  auto now = InputParser::TimePoint{};
  p.feed("\x1b[?62;c", now);
  CHECK(p.hasEvents() && p.pop().type == TermEvent::Type::REPLY);
  p.expectCursorReport();
  p.feed("\x1b[999;1R", now);
  TermEvent e = p.pop();
  CHECK(e.type == TermEvent::Type::REPLY && e.reply == ReplyKind::CURSOR_POSITION && e.params == std::vector<int>({999, 1}));
  p.feed("\x1b]11;rgb:0000/0000/0000\x1b\\", now);
  e = p.pop();
  CHECK(e.type == TermEvent::Type::REPLY && e.reply == ReplyKind::DEFAULT_COLOR && e.params == std::vector<int>({11, 0, 0, 0}));
  p.feed("\x1bP1+r7878=31\x1b\\", now);
  e = p.pop();
  CHECK(e.reply == ReplyKind::XTGETTCAP && e.text == "7878=31");
  CHECK(!p.hasEvents());
}

TEST(parser_reply_split_across_reads) {
  InputParser p;
  std::string reply = "\x1bP>|XTerm(390)\x1b\\\x1b[?64;4c";
  auto now = InputParser::TimePoint{};
  for (char c : reply) p.feed(&c, 1, now);
  TermEvent a = p.pop();
  CHECK(a.reply == ReplyKind::XTVERSION && a.text == "XTerm(390)");
  TermEvent b = p.pop();
  CHECK(b.reply == ReplyKind::PRIMARY_DA && b.params == std::vector<int>({64, 4}));
  CHECK(!p.hasEvents());
}

TEST(parser_unknown_sequence_is_consumed) {
  CHECK(same(run("\x1b[5;7~x"), v({"Ctrl+Alt+PgUp", "x"})));
  CHECK(same(run("\x1b[99;7~x"), v({"unknown", "x"})));
  CHECK(same(run("\x1b[99;1z"), v({"unknown"})));
}

TEST(parser_sgr_mouse) {
  CHECK(same(run("\x1b[<0;5;3M"), v({"Button1 press at 2,4"})));
  CHECK(same(run("\x1b[<0;5;3m"), v({"Button1 release at 2,4"})));
  CHECK(same(run("\x1b[<32;6;3M"), v({"Button1 drag at 2,5"})));
  CHECK(same(run("\x1b[<35;7;4M"), v({"Motion move at 3,6"})));
  CHECK(same(run("\x1b[<64;1;1M"), v({"WheelUp press at 0,0"})));
  CHECK(same(run("\x1b[<65;1;1M"), v({"WheelDown press at 0,0"})));
  CHECK(same(run("\x1b[<16;5;3M"), v({"Ctrl+Button1 press at 2,4"})));
  CHECK(same(run("\x1b[<4;5;3M"), v({"Shift+Button1 press at 2,4"})));
  CHECK(same(run("\x1b[<8;5;3M"), v({"Alt+Button1 press at 2,4"})));
  InputParser p;
  p.setScreenSize(24, 80);
  CHECK(same(run(p, {{0, "\x1b[<0;81;25M"}}), v({})));
  InputParser px;
  px.setPixelMouse(true, 20, 10);
  CHECK(same(run(px, {{0, "\x1b[<0;25;45M"}}), v({"Button1 press at 2,2"})));
}

TEST(parser_pixel_mouse_keeps_the_position_within_the_cell) {
  InputParser px;
  px.setPixelMouse(true, 20, 10);
  px.feed("\x1b[<35;25;45M", InputParser::TimePoint{});  // pixel x 25, y 45: cell 2,2
  CHECK(px.hasEvents());
  TermEvent e = px.pop();
  CHECK(e.y == 2 && e.x == 2);
  CHECK_NEAR(e.fy, 2.225f, 1e-4);
  CHECK_NEAR(e.fx, 2.45f, 1e-4);
  // Cell reports have no fraction.
  InputParser cells;
  cells.feed("\x1b[<35;5;3M", InputParser::TimePoint{});
  TermEvent c = cells.pop();
  CHECK_NEAR(c.fy, 2.0f, 1e-6);
  CHECK_NEAR(c.fx, 4.0f, 1e-6);
}

TEST(parser_x10_mouse) {
  CHECK(same(run("\x1b[M #$"), v({"Button1 press at 3,2"})));
  CHECK(same(run("\x1b[M##$"), v({"Button1 release at 3,2"})));
}

TEST(parser_kitty_keys) {
  InputParser p;
  p.setKittyKeyboard(true);
  CHECK(same(run(p, {{0, "\x1b[97u"}}), v({"a press"})));
  CHECK(same(run(p, {{0, "\x1b[97;1:3u"}}), v({"a release"})));
  CHECK(same(run(p, {{0, "\x1b[97;1:2u"}}), v({"a repeat"})));
  CHECK(same(run(p, {{0, "\x1b[97;5u"}}), v({"Ctrl+A press"})));
  CHECK(same(run(p, {{0, "\x1b[97;9u"}}), v({"Super+a press"})));
  CHECK(same(run(p, {{0, "\x1b[97;33u"}}), v({"Meta+a press"})));
  CHECK(same(run(p, {{0, "\x1b[49;2;33u"}}), v({"Shift+1 press text \"!\""})));
  CHECK(same(run(p, {{0, "\x1b[27u"}}, 0), v({"Esc press"})));
  CHECK(same(run(p, {{0, "\x1b[13u"}}), v({"Enter press"})));
  CHECK(same(run(p, {{0, "\x1b[127u"}}), v({"Backspace press"})));
  CHECK(same(run(p, {{0, "\x1b[57399u"}}), v({"0 press"})));
  CHECK(same(run(p, {{0, "\x1b[57441;2u"}}), v({"Shift+LeftShift press"})));
  CHECK(same(run(p, {{0, "\x1b[1;1:3A"}}), v({"Up release"})));
  CHECK(same(run(p, {{0, "\x1b[3;1:3~"}}), v({"Delete release"})));
  CHECK(same(run(p, {{0, "\x1b[15;5:2~"}}), v({"Ctrl+F5 repeat"})));
  CHECK(same(run(p, {{0, "\x1b[1;1:3P"}}), v({"F1 release"})));
  CHECK(same(run(p, {{0, "\x1b[99;5u"}}), v({"Ctrl+C press"})));
  CHECK(same(run(p, {{0, "\x1b[57410;5u"}}), v({"Ctrl+KpDivide press"})));
  CHECK(same(run(p, {{0, "\x1b[57376u"}}), v({"F13 press"})));
}

TEST(parser_string_introducers_are_alt_keys_unless_a_reply_follows) {
  CHECK(same(run({{0, "\x1bP"}, {100, ""}}), v({"Alt+P"})));
  CHECK(same(run("\x1bPq"), v({"Alt+P", "q"})));
  CHECK(same(run("\x1b]x"), v({"Alt+]", "x"})));
  CHECK(same(run("\x1b_x"), v({"Alt+_", "x"})));
  CHECK(same(run("\x1b[") , v({"Alt+["})));
  CHECK(same(run("\x1bO"), v({"Alt+O"})));
  CHECK(same(run("\x1b]11;rgb:0/0/0\x1b\\"), v({"reply"})));
  // A partial string is held longer than a key sequence, then dropped.
  CHECK(same(run({{0, "\x1b]11;rgb:00"}, {500, "00/0000/0000\x1b\\"}}), v({"reply"})));
  CHECK(same(run({{0, "\x1b]11;rgb:00"}, {1500, "x"}}), v({"x"})));
}

TEST(parser_end_of_input) {
  InputParser p;
  p.feed("\x1b", InputParser::TimePoint{});
  p.finish();
  CHECK(describe(p.pop()) == "Esc");
  CHECK(describe(p.pop()) == "end");
  p.feed("a", InputParser::TimePoint{});
  CHECK(!p.hasEvents());
}

TEST(term_event_to_input) {
  Input a = toInput(TermEvent::makeKey('A', kModShift));
  CHECK(a.getId() == 'a' && a.getTrueCaseId() == 'A' && a.hasShift() && !a.hasCtrl());
  Input legacy_upper = toInput(TermEvent::makeKey('A'));
  CHECK(legacy_upper.getId() == 'a' && legacy_upper.hasShift());
  Input ctrl_a = toInput(TermEvent::makeKey('A', kModCtrl));
  CHECK(ctrl_a.getId() == 'a' && ctrl_a.hasCtrl() && !ctrl_a.hasShift());
  TermEvent shifted = TermEvent::makeKey('1', kModShift, TermEvent::Kind::PRESS);
  shifted.text = "!";
  Input bang = toInput(shifted);
  CHECK(bang.getId() == '!' && bang.getKind() == Input::Kind::PRESS);
  TermEvent drag = TermEvent::makeKey(cellui::Key::Button1, 0, TermEvent::Kind::DRAG);
  drag.type = TermEvent::Type::MOUSE;
  drag.y = 3;
  drag.x = 4;
  Input d = toInput(drag);
  CHECK(d.getId() == cellui::Key::Button1 && d.getKind() == Input::Kind::PRESS && d.getY() == 3 && d.getX() == 4);
}

TEST(parser_dec_cursor_report_is_a_reply_only_while_one_is_expected) {
  InputParser p;
  auto t = InputParser::TimePoint{} + Ms(500);
  p.expectDecCursorReport();
  p.feed("\x1b[?5;1R", t);
  TermEvent e = p.pop();
  CHECK(e.type == TermEvent::Type::REPLY && e.reply == ReplyKind::DEC_CURSOR_POSITION && e.params == std::vector<int>({5, 1}));
  CHECK(e.at == t);  // when it was read
  p.feed("\x1b[?5;1R", t);
  CHECK(p.pop().type == TermEvent::Type::UNKNOWN);
}

TEST(parser_waits_for_the_rest_of_a_csi_with_parameters) {
  InputParser p;
  auto t = InputParser::TimePoint{};
  p.expectDecCursorReport();
  p.feed("\x1b[?5;", t);
  p.expire(t + Ms(200));  // long past the grace of a lone Escape
  CHECK(!p.hasEvents());
  p.feed("1R", t + Ms(300));
  TermEvent e = p.pop();
  CHECK(e.reply == ReplyKind::DEC_CURSOR_POSITION && !p.hasEvents());
}

TEST(parser_takes_a_late_reply_after_a_settled_escape_without_text) {
  InputParser p;
  auto t = InputParser::TimePoint{};
  p.expectDecCursorReport();
  p.feed("\x1b", t);
  p.expire(t + Ms(30));
  CHECK(p.pop().key == Key::Escape && !p.hasEvents());  // the Escape key stays
  p.feed("[?5;1R", t + Ms(300));
  TermEvent e = p.pop();
  CHECK(e.type == TermEvent::Type::REPLY && e.params == std::vector<int>({5, 1}));
  CHECK(!p.hasEvents());
}

TEST(parser_takes_a_late_reply_after_a_settled_esc_bracket) {
  InputParser p;
  auto t = InputParser::TimePoint{};
  p.expectDecCursorReport();
  p.feed("\x1b[", t);
  p.expire(t + Ms(30));
  CHECK(describe(p.pop()) == describe(TermEvent::makeKey('[', kModAlt)) && !p.hasEvents());
  p.feed("?5;1R", t + Ms(300));
  CHECK(p.pop().type == TermEvent::Type::REPLY && !p.hasEvents());
}

TEST(parser_late_bracket_is_typed_text_when_no_reply_is_expected_or_too_late) {
  InputParser p;
  auto t = InputParser::TimePoint{};
  p.feed("\x1b", t);
  p.expire(t + Ms(30));
  CHECK(p.pop().key == Key::Escape);
  p.feed("[?", t + Ms(300));
  CHECK(p.pop().key == '[');
  CHECK(p.pop().key == '?');
  // Expected, but the Escape was long ago.
  p.expectDecCursorReport();
  p.feed("\x1b", t + Ms(400));
  p.expire(t + Ms(450));
  CHECK(p.pop().key == Key::Escape);
  p.feed("[x", t + Ms(3000));
  CHECK(p.pop().key == '[');
  CHECK(p.pop().key == 'x');
}

TEST(parser_a_bracket_after_a_settled_escape_that_is_not_a_reply_is_replayed) {
  InputParser p;
  auto t = InputParser::TimePoint{};
  p.expectDecCursorReport();
  p.feed("\x1b", t);
  p.expire(t + Ms(30));
  CHECK(p.pop().key == Key::Escape);
  p.feed("[a", t + Ms(100));
  CHECK(p.pop().key == '[');
  CHECK(p.pop().key == 'a');
  CHECK(!p.hasEvents());
}
