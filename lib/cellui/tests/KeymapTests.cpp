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
  CHECK(map.isPrefix(cx));
  CHECK(!map.isPrefix(cc));
  CHECK(map.lookup(cx) == nullptr);
  CHECK(map.lookupPrefixed(cx, cc) && *map.lookupPrefixed(cx, cc) == "quit");
  CHECK(map.allBoundIn(reg));
  map.bind(cc, "typo");
  CHECK(!map.allBoundIn(reg));
}
