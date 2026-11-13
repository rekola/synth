#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// SYNTH_AUDIO_STATS=<file>: a line a second of what the audio thread paid for its blocks, to see what the
// UI costs it: the blocks, the CPU time rendering them (total and worst), how late a block started
// against the block period (a late block is one whose start came more than half a period late: the
// output ran short unless the device queue held it), and the device's xruns. Off, it costs a check a block.
class AudioStats {
 public:
  AudioStats() {
    if (const char * path = std::getenv("SYNTH_AUDIO_STATS")) file_ = std::fopen(path, "a");
  }
  ~AudioStats() {
    if (file_) std::fclose(file_);
  }
  AudioStats(const AudioStats &) = delete;
  AudioStats & operator=(const AudioStats &) = delete;

  bool on() const { return file_ != nullptr; }

  // A block starts: `period_s` is its length in seconds.
  void begin(double period_s) {
    if (!file_) return;
    period_s_ = period_s;
    auto now = std::chrono::steady_clock::now();
    if (last_start_ != std::chrono::steady_clock::time_point{}) {
      double gap = std::chrono::duration<double>(now - last_start_).count();
      if (gap > gap_max_) gap_max_ = gap;
      if (gap > 1.5 * period_s) late_++;
    }
    last_start_ = now;
    if (window_start_ == std::chrono::steady_clock::time_point{}) window_start_ = now;
    cpu_start_ = cpuNs();
  }

  // The block is rendered (before it is written to the device).
  void rendered() {
    if (!file_) return;
    int64_t spent = cpuNs() - cpu_start_;
    blocks_++;
    cpu_total_ += spent;
    if (spent > cpu_max_) cpu_max_ = spent;
  }

  // Writes the line when a second has passed.
  void log(uint64_t xruns) {
    if (!file_) return;
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - window_start_).count();
    if (dt < 1.0) return;
    std::fprintf(file_, "t=%.2f blocks=%zu period_ms=%.2f render_cpu_us=%lld max_render_us=%lld late=%zu max_gap_ms=%.2f xruns=%llu\n", dt,
                 blocks_, period_s_ * 1000.0, static_cast<long long>(cpu_total_ / 1000), static_cast<long long>(cpu_max_ / 1000), late_,
                 gap_max_ * 1000.0, static_cast<unsigned long long>(xruns - xruns_logged_));
    std::fflush(file_);
    xruns_logged_ = xruns;
    window_start_ = now;
    blocks_ = late_ = 0;
    cpu_total_ = cpu_max_ = 0;
    gap_max_ = 0;
  }

 private:
  static int64_t cpuNs() {
#ifdef _WIN32
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
  }

  std::FILE * file_ = nullptr;
  double period_s_ = 0;
  std::chrono::steady_clock::time_point last_start_{}, window_start_{};
  int64_t cpu_start_ = 0, cpu_total_ = 0, cpu_max_ = 0;
  size_t blocks_ = 0, late_ = 0;
  double gap_max_ = 0;
  uint64_t xruns_logged_ = 0;
};
