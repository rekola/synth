#include <cellui/term/KittyImages.h>

#include <cstring>

namespace cellui::term {

namespace {

// ESC _ G a=d,d=I,i=<id>,q=1 ESC \ into `out`; the bytes made. Async-signal-safe.
size_t formatDelete(char * out, uint32_t id) {
  static const char head[] = "\x1b_Ga=d,d=I,i=";
  static const char tail[] = ",q=1\x1b\\";
  size_t n = sizeof head - 1;
  std::memcpy(out, head, n);
  char digits[10];
  size_t count = 0;
  do {
    digits[count++] = static_cast<char>('0' + id % 10);
    id /= 10;
  } while (id != 0);
  while (count > 0) out[n++] = digits[--count];
  std::memcpy(out + n, tail, sizeof tail - 1);
  return n + sizeof tail - 1;
}

constexpr size_t kMaxDelete = 40;

}  // namespace

KittyImageRegistry::KittyImageRegistry() : slots_(new std::atomic<uint32_t>[kCapacity]) {
  for (size_t i = 0; i < kCapacity; i++) slots_[i].store(0, std::memory_order_relaxed);
}

uint32_t KittyImageRegistry::allocate() {
  uint32_t id = last_id_;
  do {
    if (++id == 0) id = 1;
  } while (index_.count(id));
  last_id_ = id;
  // A full array leaves the id untracked: exit then misses it, but nothing else breaks.
  for (size_t tried = 0; tried < kCapacity; tried++) {
    size_t slot = (next_slot_ + tried) % kCapacity;
    if (slots_[slot].load(std::memory_order_relaxed) != 0) continue;
    slots_[slot].store(id, std::memory_order_release);
    index_[id] = slot;
    next_slot_ = slot + 1;
    break;
  }
  return id;
}

void KittyImageRegistry::forget(uint32_t id) {
  auto it = index_.find(id);
  if (it == index_.end()) return;
  slots_[it->second].store(0, std::memory_order_release);
  next_slot_ = it->second;
  index_.erase(it);
}

std::string KittyImageRegistry::deleteSequence(uint32_t id) {
  char buf[kMaxDelete];
  return std::string(buf, formatDelete(buf, id));
}

std::string KittyImageRegistry::cleanupSequence() const {
  std::string out;
  for (size_t i = 0; i < kCapacity; i++) {
    uint32_t id = slots_[i].load(std::memory_order_acquire);
    if (id != 0) out += deleteSequence(id);
  }
  return out;
}

size_t KittyImageRegistry::formatCleanup(char * out, size_t cap, size_t & from) const noexcept {
  size_t used = 0;
  while (from < kCapacity && used + kMaxDelete <= cap) {
    uint32_t id = slots_[from++].load(std::memory_order_acquire);
    if (id != 0) used += formatDelete(out + used, id);
  }
  return used;
}

}  // namespace cellui::term
