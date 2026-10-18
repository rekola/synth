#include "NullAudio.h"
#include "AudioBuffer.h"

#include <algorithm>

using namespace std::chrono;

WaitResult
NullAudio::wait(Notifier & wake, bool, int timeout_ms) {
  if (!started_) {
    started_ = true;
    next_block_ = Clock::now();
  }

  WaitResult result;
  auto now = Clock::now();
  auto until_block = std::max<long long>(0, duration_cast<milliseconds>(next_block_ - now).count());
  // Round up so waking early never reports a block that is not yet due.
  if (next_block_ > now && duration_cast<milliseconds>(next_block_ - now).count() == 0) until_block = 1;
  auto sleep_ms = static_cast<int>(timeout_ms < 0 ? until_block : std::min<long long>(timeout_ms, until_block));

  result.woken = wake.wait(sleep_ms);
  result.playback = Clock::now() >= next_block_;
  return result;
}

void
NullAudio::play(const AudioBuffer &, Logger &) {
  blocks_played_++;
  next_block_ += duration_cast<Clock::duration>(duration<double>(static_cast<double>(block_frames_) / getFrequency()));
  // After a stall, resume at the clock instead of catching up in a burst.
  auto now = Clock::now();
  if (next_block_ + seconds(1) < now) next_block_ = now;
}

AudioBuffer
NullAudio::record(Logger &) {
  return AudioBuffer();
}
