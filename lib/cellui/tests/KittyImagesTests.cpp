#include "TestFramework.h"

#include <cellui/term/ImageEncoder.h>
#include <cellui/term/KittyImages.h>

#include <set>

using namespace cellui::term;

TEST(kitty_registry_tracks_allocated_ids_until_forgotten) {
  KittyImageRegistry r;
  uint32_t a = r.allocate(), b = r.allocate(), c = r.allocate();
  CHECK(a != 0 && b != 0 && c != 0 && a != b && b != c && a != c);
  CHECK(r.size() == 3 && r.tracked(a) && r.tracked(b) && r.tracked(c));
  r.forget(b);
  CHECK(r.size() == 2 && !r.tracked(b));
  r.forget(b);  // twice is harmless
  CHECK(r.size() == 2);
}

TEST(kitty_cleanup_deletes_each_tracked_id_with_its_data_and_nothing_else) {
  KittyImageRegistry r;
  CHECK(r.cleanupSequence().empty());
  uint32_t a = r.allocate(), b = r.allocate(), c = r.allocate();
  r.forget(b);
  std::string seq = r.cleanupSequence();
  std::string expect;
  std::set<uint32_t> want{a, c};
  for (uint32_t id : want) CHECK(seq.find(KittyImageRegistry::deleteSequence(id)) != std::string::npos);
  CHECK(seq.find(KittyImageRegistry::deleteSequence(b)) == std::string::npos);
  CHECK(KittyImageRegistry::deleteSequence(7) == "\x1b_Ga=d,d=I,i=7,q=1\x1b\\");
  // Only per-id, capital-I deletions: never a delete-all, never placements alone.
  size_t count = 0;
  for (size_t at = seq.find("\x1b_G"); at != std::string::npos; at = seq.find("\x1b_G", at + 1)) count++;
  CHECK(count == 2);
  CHECK(seq.find("a=d,q=1") == std::string::npos && seq.find("d=A") == std::string::npos && seq.find("d=a") == std::string::npos);
  CHECK(seq.find("d=i") == std::string::npos);
}

TEST(kitty_cleanup_formats_the_same_way_for_a_signal_handler) {
  KittyImageRegistry r;
  for (int i = 0; i < 300; i++) r.allocate();
  std::string joined;
  char buf[128];  // small: forces several rounds
  size_t from = 0;
  while (from < KittyImageRegistry::kCapacity) joined.append(buf, r.formatCleanup(buf, sizeof buf, from));
  std::set<std::string> a, b;
  auto split = [](const std::string & s, std::set<std::string> & out) {
    for (size_t at = 0; at < s.size();) {
      size_t end = s.find("\x1b\\", at) + 2;
      out.insert(s.substr(at, end - at));
      at = end;
    }
  };
  split(joined, a);
  split(r.cleanupSequence(), b);
  CHECK(a.size() == 300 && a == b);
}

TEST(kitty_ids_of_every_image_feature_are_tracked_and_freed_with_clear) {
  auto registry = std::make_shared<KittyImageRegistry>();
  ImageEncoder::Profile profile;
  profile.protocol = PixelProtocol::KITTY;
  profile.cell_px_width = 6;
  profile.cell_px_height = 12;
  ImageEncoder enc(profile, registry);
  CHECK(enc.registry() == registry);
  // Borders are one feature that transmits images of its own.
  cellui::CellBuffer frame(4, 8);
  cellui::CellBorder border;
  border.row = 1;
  border.col = 1;
  border.edges = 0xF;
  border.color = cellui::Color(255, 0, 0);
  std::string sent = enc.encode(frame, {}, false, {border});
  CHECK(sent.find("a=t,") != std::string::npos);
  CHECK(registry->size() == 4);
  // Exit deletes exactly those ids.
  std::string cleanup = registry->cleanupSequence();
  size_t deletions = 0;
  for (size_t at = cleanup.find("a=d,d=I,i="); at != std::string::npos; at = cleanup.find("a=d,d=I,i=", at + 1)) deletions++;
  CHECK(deletions == 4);
  enc.clear();
  CHECK(registry->size() == 0 && registry->cleanupSequence().empty());
}
