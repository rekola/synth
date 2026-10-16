#include "TestFramework.h"

#include <cellui/CommandRegistry.h>
#include <cellui/KeyChord.h>
#include <cellui/Keymap.h>

using namespace cellui;

TEST(keychord_pack_tells_modifiers_apart) {
  CHECK(KeyChord::pack('x', true, false, false, false) != KeyChord::pack('x', false, false, false, false));
  CHECK(KeyChord::pack('x', false, true, false, false) != KeyChord::pack('x', false, false, true, false));
  CHECK(KeyChord::pack('x', false, false, false, true) != KeyChord::pack('y', false, false, false, true));
}

TEST(registry_executes_and_completes_by_prefix) {
  CommandRegistry reg;
  int ran = 0;
  reg.define("set-mark", [&] { ran++; });
  reg.define("set-mode", [] {});
  CHECK(reg.has("set-mark"));
  CHECK(reg.execute("set-mark"));
  CHECK(ran == 1);
  CHECK(!reg.execute("nope"));
  CHECK(reg.matching("set-ma").size() == 1);
  CHECK(reg.matching("set-").size() == 2);
  reg.undefine("set-mark");
  CHECK(!reg.has("set-mark"));
  CHECK(reg.matching("set-").size() == 1);
}

TEST(keymap_binds_prefixes_and_checks_names) {
  CommandRegistry reg;
  reg.define("quit", [] {});
  Keymap map;
  auto cx = KeyChord::pack('x', true, false, false, false);
  auto cc = KeyChord::pack('c', true, false, false, false);
  map.bindPrefixed(cx, cc, "quit");
  CHECK(map.isPrefix(cx, KeymapProfile::EMACS));
  CHECK(!map.isPrefix(cc, KeymapProfile::EMACS));
  CHECK(map.lookup(cx, KeymapProfile::EMACS) == nullptr);
  CHECK(map.lookupPrefixed(cx, cc, KeymapProfile::EMACS) && *map.lookupPrefixed(cx, cc, KeymapProfile::EMACS) == "quit");
  CHECK(map.allBoundIn(reg));
  map.bind(cc, "typo");
  CHECK(!map.allBoundIn(reg));
}

TEST(keymap_profiles_keep_their_own_bindings) {
  using P = KeymapProfile;
  Keymap map;
  auto cy = KeyChord::pack('y', true, false, false, false);
  auto cx = KeyChord::pack('x', true, false, false, false);
  auto cs = KeyChord::pack('s', true, false, false, false);
  map.bind(cy, "shared");
  map.bind(P::EMACS, cy, "yank");
  map.bind(P::DESKTOP, cx, "cut");
  map.bindPrefixed(P::EMACS, cx, cs, "save");
  // A profile's own binding wins over a shared one; the other profile still
  // sees the shared one.
  CHECK(*map.lookup(cy, P::EMACS) == "yank");
  CHECK(*map.lookup(cy, P::DESKTOP) == "shared");
  // A chord bound for one profile is unbound in the other, prefixes included.
  CHECK(map.lookup(cx, P::EMACS) == nullptr);
  CHECK(*map.lookup(cx, P::DESKTOP) == "cut");
  CHECK(map.isPrefix(cx, P::EMACS));
  CHECK(!map.isPrefix(cx, P::DESKTOP));
  CHECK(map.lookupPrefixed(cx, cs, P::DESKTOP) == nullptr);
}

TEST(keychord_packs_an_input_like_its_fields) {
  Input in('x', 0, 0, false, false, true, false);
  CHECK(KeyChord::pack(in) == KeyChord::pack('x', true, false, false, false));
  CHECK(in.getTrueCaseId() == 'x');
  Input upper('x', 0, 0, false, true, false, false, Input::Kind::PRESS, 'X');
  CHECK(upper.getTrueCaseId() == 'X' && upper.getKind() == Input::Kind::PRESS);
}
