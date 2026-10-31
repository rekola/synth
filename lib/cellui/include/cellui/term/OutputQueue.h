#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cellui::term {

// What is waiting to be written to the terminal. Writing never blocks: flush()
// gives the descriptor what it takes now and keeps the rest for when it can
// take more (the descriptor is non-blocking). A long wait is normal on a
// serial line held by flow control and is no error.
class OutputQueue {
 public:
  void append(std::string_view bytes) {
    buffer_.append(bytes.data(), bytes.size());
    queued_ += bytes.size();
  }
  bool empty() const { return offset_ >= buffer_.size(); }
  size_t pending() const { return buffer_.size() - offset_; }
  // Bytes appended and bytes the descriptor has taken, since the start.
  uint64_t queued() const { return queued_; }
  uint64_t accepted() const { return queued_ - pending(); }

  // Writes what the descriptor takes now, with the termination signals
  // blocked so a restoration never lands inside what is half written.
  // False when the descriptor failed for good (the terminal is gone).
  bool flush(int fd);
  // Gives up what has not been written.
  void discard() {
    queued_ -= pending();
    buffer_.clear();
    offset_ = 0;
  }

 private:
  std::string buffer_;
  size_t offset_ = 0;
  uint64_t queued_ = 0;
};

}  // namespace cellui::term
