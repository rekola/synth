#include <cellui/TestBackend.h>

#include <cellui/Utf8.h>

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace cellui::test {

namespace {

void makePipe(int fds[2]) {
  if (::pipe(fds) != 0) {
    fds[0] = fds[1] = -1;
    return;
  }
  for (int i = 0; i < 2; i++) {
    int flags = ::fcntl(fds[i], F_GETFL);
    ::fcntl(fds[i], F_SETFL, flags | O_NONBLOCK);
    ::fcntl(fds[i], F_SETFD, FD_CLOEXEC);
  }
}

void drain(int fd) {
  char buf[256];
  while (fd >= 0 && ::read(fd, buf, sizeof buf) > 0) { }
}

void closeFd(int & fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}

}  // namespace

TestBackend::TestBackend(int rows, int cols, DisplayInfo display)
  : display_(std::move(display)), rows_(rows), cols_(cols), frame_(rows, cols) {
  makePipe(pipe_);
  makePipe(wake_);
}

TestBackend::~TestBackend() {
  for (int * fds : { pipe_, wake_ }) {
    closeFd(fds[0]);
    closeFd(fds[1]);
  }
}

std::vector<BackendEvent> TestBackend::poll() {
  drain(pipe_[0]);
  drain(wake_[0]);
  std::vector<BackendEvent> out(queue_.begin(), queue_.end());
  queue_.clear();
  return out;
}

void TestBackend::push(const BackendEvent & event) {
  queue_.push_back(event);
  char c = 'e';
  ssize_t n = ::write(pipe_[1], &c, 1);
  (void)n;
}

void TestBackend::pushText(const std::string & text) {
  for (const std::string & cluster : utf8::clusters(text)) {
    // The first codepoint of the cluster, decoded.
    unsigned char lead = static_cast<unsigned char>(cluster[0]);
    int len = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    int cp = len == 1 ? lead : lead & (0xFF >> (len + 1));
    for (int i = 1; i < len && static_cast<size_t>(i) < cluster.size(); i++) cp = (cp << 6) | (static_cast<unsigned char>(cluster[static_cast<size_t>(i)]) & 0x3F);
    pushKey(cp);
  }
}

void TestBackend::pushMouse(int y, int x, int key, unsigned mods, Input::Kind kind) {
  push(BackendEvent::makeInput(Input(key, y, x, mods & kModAlt, mods & kModShift, mods & kModCtrl, mods & (kModMeta | kModSuper), kind)));
}

void TestBackend::pushPaste(const std::string & text) {
  push(BackendEvent::makePaste(text));
}

void TestBackend::resize(int rows, int cols) {
  rows_ = rows;
  cols_ = cols;
  push(BackendEvent::makeResize(rows, cols));
}

void TestBackend::pushEnd() {
  push(BackendEvent::makeClose());
}

void TestBackend::wake() {
  char c = 'w';
  ssize_t n = ::write(wake_[1], &c, 1);
  (void)n;
}

void TestBackend::present(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images,
                           const FrameInfo * info) {
  info_ = info ? *info : FrameInfo();
  cursor_ = cursor;
  images_ = images;
  frame_ = frame;
  if (!info_.backdrops.empty() || backdrops_.holdsState()) {
    if (emulate_backdrops_) {
      backdrops_.apply(frame_, images_, info_.backdrops, display_);
    } else if (!info_.backdrops.empty()) {
      // Each layer under the next, unblurred.
      auto over = [](CellBuffer & out, const CellBuffer & cells) {
        for (int r = 0; r < std::min(out.rows(), cells.rows()); r++) {
          for (int c = 0; c < std::min(out.cols(), cells.cols()); c++) {
            if (!CellBuffer::isBlank(cells.at(r, c))) out.put(r, c, cells.at(r, c));
          }
        }
      };
      // A plane scrolled by part of a cell is shown where its cells are; the
      // fraction is in the layer for the test to read.
      auto overScroll = [&](CellBuffer & out, const BackdropLayer & layer) {
        if (!layer.scroll) return;
        const ScrollContent & content = *layer.scroll;
        for (int r = 0; r < content.rows; r++) {
          for (int c = 0; c < content.cols; c++) {
            if (!CellBuffer::isBlank(content.cells.at(r, c))) out.put(content.row + r, content.col + c, content.cells.at(r, c));
          }
        }
      };
      CellBuffer stacked = info_.backdrops[0].cells;
      overScroll(stacked, info_.backdrops[0]);
      for (size_t i = 1; i < info_.backdrops.size(); i++) {
        over(stacked, info_.backdrops[i].cells);
        overScroll(stacked, info_.backdrops[i]);
      }
      over(stacked, frame_);
      frame_ = std::move(stacked);
    }
  }
  frames_++;
}

}  // namespace cellui::test
