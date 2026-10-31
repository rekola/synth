// Drives a TerminalSession the way the application does - a frame handler that
// presents and asks for the next frame - for tools/pacing/run_assertions.py,
// which sits on the other side of a shaped pty. Everything it measures goes to
// the --out file; stdout is the terminal.
//
//   --mode churn   a picture and a counter change every frame, for --seconds
//   --mode static  N pictures drawn once, then a text change, then one picture removed
//   --img WxH      the churning picture's pixels (3 bytes each, 4/3 as base64)
//   --images N     static mode: how many pictures
#include <cellui/BufferPlane.h>
#include <cellui/StyleProvider.h>
#include <cellui/term/TerminalSession.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace cellui;
using namespace cellui::term;
using Clock = std::chrono::steady_clock;

namespace {

long long epochMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char ** argv) {
  std::string mode = "churn", out_path = "/dev/null";
  double seconds = 5;
  int img_w = 80, img_h = 80, images = 6;
  int min_interval_ms = -1;
  std::string pin;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string a = argv[i];
    if (a == "--mode") mode = argv[i + 1];
    else if (a == "--seconds") seconds = std::atof(argv[i + 1]);
    else if (a == "--out") out_path = argv[i + 1];
    else if (a == "--images") images = std::atoi(argv[i + 1]);
    else if (a == "--min-interval-ms") min_interval_ms = std::atoi(argv[i + 1]);
    else if (a == "--pin") pin = argv[i + 1];
    else if (a == "--img") std::sscanf(argv[i + 1], "%dx%d", &img_w, &img_h);
  }
  FILE * log = std::fopen(out_path.c_str(), "w");
  if (!log) return 2;
  setvbuf(log, nullptr, _IOLBF, 0);

  SessionOptions options;
  options.mouse = false;
  if (min_interval_ms >= 0) options.pacer.min_frame_interval = std::chrono::milliseconds(min_interval_ms);
  std::string error;
  auto session = TerminalSession::open(options, &error);
  if (!session) {
    std::fprintf(log, "ERROR %s\n", error.c_str());
    return 1;
  }
  int link_changes = 0;
  LinkClass link_seen = LinkClass::FULL;
  session->setLinkHandler([&](const LinkStatus & status) {
    if (status.cls != link_seen) link_changes++;  // the status becoming available is not a change of class
    link_seen = status.cls;
  });
  if (!pin.empty()) {
    std::optional<LinkClass> pinned;
    if (parseLinkClass(pin, pinned)) session->setLinkPin(pinned);
  }
  const DisplayInfo & display = session->display();
  StyleProvider styles;
  CellBuffer screen(session->rows(), session->cols());
  BufferPlane root(screen, styles);
  root.setCellPixelSize(display.cell_px_height, display.cell_px_width);

  long frame_no = 0;
  bool churn = mode == "churn";
  std::unique_ptr<Plane> chart;
  std::vector<std::unique_ptr<Plane>> pics;
  auto cells_h = [&](int px) { return (px + display.cell_px_height - 1) / display.cell_px_height; };
  auto cells_w = [&](int px) { return (px + display.cell_px_width - 1) / display.cell_px_width; };
  if (churn) {
    chart = root.createChild();
    chart->resize(cells_h(img_h), cells_w(img_w));
    chart->move(2, 2);
  } else {
    for (int i = 0; i < images; i++) {
      auto p = root.createChild();
      p->resize(cells_h(img_h), cells_w(img_w));
      p->move(2, 2 + i * (cells_w(img_w) + 1));
      std::vector<uint32_t> px(static_cast<size_t>(img_w * img_h));
      for (size_t k = 0; k < px.size(); k++) px[k] = Image::pack(static_cast<uint8_t>(k * 7 + static_cast<size_t>(i) * 40), static_cast<uint8_t>(k * 3), static_cast<uint8_t>(i * 30));
      p->drawImage(px, img_w, img_h);
      pics.push_back(std::move(p));
    }
  }
  std::string status = "start";

  auto draw = [&] {
    std::string line = status + " " + std::to_string(frame_no);
    line.resize(40, ' ');
    root.putstr(0, 0, line);
    if (churn) {
      std::vector<uint32_t> px(static_cast<size_t>(img_w * img_h));
      uint32_t seed = static_cast<uint32_t>(frame_no) * 2654435761u + 1;
      for (size_t k = 0; k < px.size(); k++) {
        seed = seed * 1664525u + 1013904223u;
        px[k] = Image::pack(static_cast<uint8_t>(seed >> 24), static_cast<uint8_t>(seed >> 16), static_cast<uint8_t>(seed >> 8));
      }
      chart->drawImage(px, img_w, img_h);
    }
    root.refresh();
  };
  auto snapshot = [&](const char * name) {
    PacerStats s = session->pacerStats();
    std::fprintf(log, "PHASE %s upload=%zu placement=%zu other=%zu frames=%zu t=%lld\n", name, s.totals.upload, s.totals.placement,
                 s.totals.other, s.frames, epochMicros());
  };

  session->setFrameHandler([&](int64_t) {
    frame_no++;
    draw();
    session->present(screen, {}, root.images());
    if (churn) session->requestFrame();
  });
  session->requestFrame();

  EventLoop & loop = session->eventLoop();
  auto start = Clock::now();
  auto end = start + std::chrono::microseconds(static_cast<long long>(seconds * 1e6));
  int phase = 0;
  double next_tick = 1.0;
  auto readInput = [&] {
    for (const BackendEvent & e : session->poll()) {
      if (e.type != BackendEvent::Type::INPUT) continue;
      std::fprintf(log, "KEY %lld %d\n", epochMicros(), e.input.getId());
    }
  };
  loop.addSource({ session->inputFd(), session->wakeFd() }, readInput);
  loop.setBefore([&]() -> int {
    auto now = Clock::now();
    long long wait = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count() + 1;
    if (!churn) wait = std::min<long long>(wait, 50);
    if (auto d = session->nextDeadline()) wait = std::min<long long>(wait, std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(*d - now).count() + 1));
    return static_cast<int>(std::max<long long>(wait, 0));
  });
  loop.setAfter([&](bool) {
    auto now = Clock::now();
    // As the application does: what the parser holds is settled when its time is up.
    if (auto d = session->nextDeadline(); d && now >= *d) readInput();
    double t = std::chrono::duration<double>(now - start).count();
    if (!churn) {
      if (phase == 0 && t > 1.5) {
        phase = 1;
        snapshot("settled");
        status = "changed";
        session->requestFrame();
      } else if (phase == 1 && t > 2.5) {
        phase = 2;
        snapshot("after-text-change");
        pics.back()->erase();
        pics.pop_back();
        session->requestFrame();
      } else if (phase == 2 && t > 3.5) {
        phase = 3;
        snapshot("after-removal");
      }
    }
    if (t >= next_tick) {
      next_tick += 1.0;
      std::fprintf(log, "TICK %lld frames=%zu\n", epochMicros(), session->pacerStats().frames);
    }
    if (now >= end) loop.quit();
  });
  loop.run();

  PacerStats s = session->pacerStats();
  double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
  std::fprintf(log,
               "RESULT frames=%zu seconds=%.3f fps=%.2f bandwidth=%.0f rtt_us=%.0f acks=%zu backoffs=%zu stalls=%zu fences=%d "
               "upload=%zu placement=%zu other=%zu kitty_errors=%zu\n",
               s.frames, elapsed, static_cast<double>(s.frames) / elapsed, s.bandwidth, s.rtt_min_us, s.acks, s.backoffs, s.stalls,
               s.fences ? 1 : 0, s.totals.upload, s.totals.placement, s.totals.other, s.kitty_errors);
  LinkStatus link = session->linkStatus();
  std::fprintf(log, "LINK class=%d name=%s pinned=%d available=%d changes=%d description=%s\n", static_cast<int>(link.cls), linkClassName(link.cls),
               link.pinned ? 1 : 0, link.available ? 1 : 0, link_changes, link.description.c_str());
  session->close();
  return 0;
}
