#include "TestFramework.h"

#include <cellui/KeyReader.h>

#include <vector>

using namespace cellui;

namespace {

Input key(int id, Input::Kind kind = Input::Kind::UNKNOWN) { return Input(id, -1, -1, false, false, false, false, kind); }

std::vector<Input> drain(KeyReader & r) {
  std::vector<Input> out;
  Input in(0, -1, -1, false, false, false, false);
  while (r.next(&in)) out.push_back(in);
  return out;
}

}  // namespace

TEST(key_reader_escape_then_key_is_only_the_chord) {
  KeyReader r;
  r.push(key(Key::Escape));
  CHECK(drain(r).empty() && r.escapePending() && r.indicatorDue());
  r.push(key('x'));
  auto out = drain(r);
  CHECK(out.size() == 1 && out[0].getId() == 'x' && out[0].hasAlt());
  CHECK(!r.escapePending() && !r.indicatorDue());
}

TEST(key_reader_escape_then_unfoldable_key_delivers_both) {
  KeyReader r;
  r.push(key(Key::Escape));
  r.push(key('g', Input::Kind::UNKNOWN));
  r.push(Input('[', -1, -1, false, false, false, false));
  auto out = drain(r);
  CHECK(out.size() == 2 && out[0].getId() == 'g' && out[0].hasAlt() && out[1].getId() == '[');
  r.push(key(Key::Escape));
  r.push(Input('g', -1, -1, false, false, true, false));  // Ctrl-g: never folded
  out = drain(r);
  CHECK(out.size() == 2 && out[0].getId() == Key::Escape && out[1].getId() == 'g' && out[1].hasCtrl());
}

TEST(key_reader_kitty_releases_and_mouse_do_not_decide) {
  KeyReader r;
  r.push(key(Key::Escape, Input::Kind::PRESS));
  r.push(key(Key::Escape, Input::Kind::RELEASE));
  r.push(key(Key::Button1, Input::Kind::PRESS));
  r.push(key('x', Input::Kind::PRESS));
  r.push(key('x', Input::Kind::RELEASE));
  auto out = drain(r);
  CHECK(out.size() == 3);
  CHECK(out[0].getId() == Key::Button1);
  CHECK(out[1].getId() == 'x' && out[1].hasAlt() && out[1].getKind() == Input::Kind::PRESS);
  CHECK(out[2].getId() == 'x' && out[2].hasAlt() && out[2].getKind() == Input::Kind::RELEASE);
}

TEST(key_reader_a_taken_escape_is_not_delivered_again) {
  KeyReader r;
  int asked = 0;
  r.takeEscape = [&](const Input &) {
    asked++;
    return true;
  };
  r.push(key(Key::Escape));
  r.push(key(Key::Down));
  auto out = drain(r);
  CHECK(asked == 1 && out.size() == 1 && out[0].getId() == Key::Down);
  // Taken, it can still be a prefix.
  r.push(key(Key::Escape));
  r.push(key('x'));
  out = drain(r);
  CHECK(asked == 2 && out.size() == 1 && out[0].getId() == 'x' && out[0].hasAlt());
}

TEST(key_reader_without_escape_as_meta_delivers_escape_at_once) {
  KeyReader r;
  r.setEscapeAsMeta(false);
  r.push(key(Key::Escape));
  auto out = drain(r);
  CHECK(out.size() == 1 && out[0].getId() == Key::Escape && !r.escapePending() && !r.indicatorDue());
  r.push(key('x'));
  out = drain(r);
  CHECK(out.size() == 1 && out[0].getId() == 'x' && !out[0].hasAlt());
}

TEST(key_reader_without_escape_as_meta_still_offers_escape_to_take) {
  KeyReader r;
  r.setEscapeAsMeta(false);
  r.takeEscape = [](const Input &) { return true; };
  r.push(key(Key::Escape, Input::Kind::PRESS));
  r.push(key(Key::Escape, Input::Kind::RELEASE));
  CHECK(drain(r).empty());
}
