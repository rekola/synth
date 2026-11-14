#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace cellui::term {

// Every kitty graphics image id the backend has transmitted, in one place for all image features, so
// that exit can delete exactly those and nothing another program in the terminal made. Ids come from
// allocate(); forget() is called once an image was deleted on the wire. The ids sit in a fixed array of
// atomics so a signal handler can read them (writeCleanup()) without locks or allocation; only the
// UI thread adds or removes.
class KittyImageRegistry {
 public:
  // The most ids tracked at once (the terminal's own memory budget keeps the real count far below).
  static constexpr size_t kCapacity = 1u << 16;

  KittyImageRegistry();

  // A new id, not in use, tracked from now on.
  uint32_t allocate();
  // The image with this id was deleted from the terminal.
  void forget(uint32_t id);
  size_t size() const { return index_.size(); }
  bool tracked(uint32_t id) const { return index_.count(id) != 0; }

  // The deletions of every tracked image, placements and stored data (`d=I`), by id; never a delete-all.
  std::string cleanupSequence() const;
  // The same into a caller's buffer, async-signal-safe: fills `out` (`cap` bytes) with as many deletions
  // as fit, from slot `from` (start at 0; advanced), and returns the bytes made. Done when `from`
  // reaches kCapacity.
  size_t formatCleanup(char * out, size_t cap, size_t & from) const noexcept;
  // The one deletion of an id.
  static std::string deleteSequence(uint32_t id);

 private:
  std::unique_ptr<std::atomic<uint32_t>[]> slots_;
  std::unordered_map<uint32_t, size_t> index_;  // id -> slot (UI thread only)
  size_t next_slot_ = 0;
  uint32_t last_id_ = 0;
};

}  // namespace cellui::term
