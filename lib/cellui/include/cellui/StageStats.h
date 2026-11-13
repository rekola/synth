#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>

#ifdef _WIN32
#include <chrono>
#endif

namespace cellui {

// Where the UI thread spends its CPU on pictures and frames, by stage, for a log that says what a slow
// machine pays for each. Stages nest (an image encode holds its deflate and its palette), so a stage's
// figure is its own total, never to be summed with the ones inside it.
enum class Stage {
  PICTURE_RENDER,  // a widget's picture drawn with canvas (dial, line, heatmap, sparkline, waveform)
  BACKDROP,        // the terminal's blur: a backdrop drawn, blurred and stretched in tiles
  CELL_ENCODE,     // the frame's cells as escape sequences
  IMAGE_ENCODE,    // every picture of a frame as bytes for the terminal (all of the stages below)
  DEFLATE,         // zlib compression of picture bytes
  BASE64,          // base64 of a payload
  PNG,             // the PNG of a picture (its filtering, palette and quantising; the deflate is counted apart)
  SIXEL_PALETTE,   // the colours of a Sixel picture: its exact colours or the median cut
  SIXEL_MAP,       // each pixel to a register, with the dither
  SIXEL_ENCODE,    // a Sixel picture's bytes (holds the map)
  COUNT
};

constexpr const char * stageName(Stage s) {
  switch (s) {
    case Stage::PICTURE_RENDER: return "picture_render";
    case Stage::BACKDROP: return "backdrop";
    case Stage::CELL_ENCODE: return "cell_encode";
    case Stage::IMAGE_ENCODE: return "image_encode";
    case Stage::DEFLATE: return "deflate";
    case Stage::BASE64: return "base64";
    case Stage::PNG: return "png";
    case Stage::SIXEL_PALETTE: return "sixel_palette";
    case Stage::SIXEL_MAP: return "sixel_map";
    case Stage::SIXEL_ENCODE: return "sixel_encode";
    case Stage::COUNT: break;
  }
  return "";
}

constexpr size_t kStageCount = static_cast<size_t>(Stage::COUNT);

// Totals in nanoseconds of the calling thread's CPU time (wall time where the platform gives none), kept
// only while switched on, so a build that never asks costs one load a stage.
class StageStats {
 public:
  static bool enabled() { return on().load(std::memory_order_relaxed); }
  static void enable(bool on_now = true) { on().store(on_now, std::memory_order_relaxed); }
  static void add(Stage s, int64_t ns) { total(s).fetch_add(ns, std::memory_order_relaxed); }
  static std::array<int64_t, kStageCount> totals() {
    std::array<int64_t, kStageCount> out{};
    for (size_t i = 0; i < kStageCount; i++) out[i] = total(static_cast<Stage>(i)).load(std::memory_order_relaxed);
    return out;
  }
  // CPU time of this thread so far.
  static int64_t cpuNs() {
#ifdef _WIN32
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
  }

 private:
  static std::atomic<bool> & on() {
    static std::atomic<bool> value{false};
    return value;
  }
  static std::atomic<int64_t> & total(Stage s) {
    static std::array<std::atomic<int64_t>, kStageCount> totals{};
    return totals[static_cast<size_t>(s)];
  }
};

// Counts the CPU time of its scope towards a stage.
class StageTimer {
 public:
  explicit StageTimer(Stage stage) : stage_(stage), start_(StageStats::enabled() ? StageStats::cpuNs() : -1) { }
  ~StageTimer() {
    if (start_ >= 0) StageStats::add(stage_, StageStats::cpuNs() - start_);
  }
  StageTimer(const StageTimer &) = delete;
  StageTimer & operator=(const StageTimer &) = delete;

 private:
  Stage stage_;
  int64_t start_;
};

}  // namespace cellui
