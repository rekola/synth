#ifndef _MONOFIFO_H_
#define _MONOFIFO_H_

#include <algorithm>
#include <cstddef>
#include <vector>

namespace dsp {

// A fixed-capacity, single-threaded mono sample queue between two
// block-driven streams running at the same rate but not in lockstep -
// live input monitoring's capture and playback. A pull short of data
// fills the rest with silence; a push past capacity drops the oldest
// samples, as does trimTo(), which bounds the latency the queue adds.
class MonoFifo {
public:
  void setCapacity(size_t capacity) {
    buffer_.assign(capacity, 0.0f);
    read_ = count_ = 0;
  }

  size_t size() const { return count_; }
  size_t capacity() const { return buffer_.size(); }

  void push(const float * samples, size_t n) {
    if (buffer_.empty()) return;
    for (size_t i = 0; i < n; i++) {
      if (count_ == buffer_.size()) drop(1);
      buffer_[(read_ + count_) % buffer_.size()] = samples[i];
      count_++;
    }
  }

  void pull(float * out, size_t n) {
    size_t available = std::min(n, count_);
    for (size_t i = 0; i < available; i++) out[i] = buffer_[(read_ + i) % buffer_.size()];
    std::fill(out + available, out + n, 0.0f);
    drop(available);
  }

  void trimTo(size_t max_size) {
    if (count_ > max_size) drop(count_ - max_size);
  }

  void clear() { read_ = count_ = 0; }

private:
  void drop(size_t n) {
    read_ = buffer_.empty() ? 0 : (read_ + n) % buffer_.size();
    count_ -= n;
  }

  std::vector<float> buffer_;
  size_t read_ = 0, count_ = 0;
};

}

#endif
