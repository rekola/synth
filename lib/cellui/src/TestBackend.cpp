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

TestBackend::TestBackend(int rows, int cols, term::Capabilities caps)
  : caps_(std::move(caps)), rows_(rows), cols_(cols), frame_(rows, cols) {
  caps_.rows = rows;
  caps_.cols = cols;
  makePipe(pipe_);
  makePipe(wake_);
}

TestBackend::~TestBackend() {
  for (int * fds : { pipe_, wake_ }) {
    closeFd(fds[0]);
    closeFd(fds[1]);
  }
}

std::vector<term::TermEvent> TestBackend::poll() {
  drain(pipe_[0]);
  drain(wake_[0]);
  std::vector<term::TermEvent> out(queue_.begin(), queue_.end());
  queue_.clear();
  return out;
}

void TestBackend::push(const term::TermEvent & event) {
  queue_.push_back(event);
  char c = 'e';
  ssize_t n = ::write(pipe_[1], &c, 1);
  (void)n;
}

void TestBackend::pushText(const std::string & text) {
  for (const std::string & cluster : utf8::clusters(text)) {
    term::TermEvent e = term::TermEvent::makeKey(0);
    e.text = cluster;
    // The first codepoint of the cluster, decoded.
    unsigned char lead = static_cast<unsigned char>(cluster[0]);
    int len = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    int cp = len == 1 ? lead : lead & (0xFF >> (len + 1));
    for (int i = 1; i < len && static_cast<size_t>(i) < cluster.size(); i++) cp = (cp << 6) | (static_cast<unsigned char>(cluster[static_cast<size_t>(i)]) & 0x3F);
    e.key = cp;
    push(e);
  }
}

void TestBackend::pushMouse(int y, int x, int key, unsigned mods, term::TermEvent::Kind kind) {
  term::TermEvent e;
  e.type = term::TermEvent::Type::MOUSE;
  e.kind = kind;
  e.key = key;
  e.mods = mods;
  e.y = y;
  e.x = x;
  push(e);
}

void TestBackend::pushPaste(const std::string & text) {
  term::TermEvent e;
  e.type = term::TermEvent::Type::PASTE;
  e.text = text;
  push(e);
}

void TestBackend::resize(int rows, int cols) {
  rows_ = rows;
  cols_ = cols;
  caps_.rows = rows;
  caps_.cols = cols;
  term::TermEvent e;
  e.type = term::TermEvent::Type::RESIZE;
  e.rows = rows;
  e.cols = cols;
  push(e);
}

void TestBackend::pushEnd() {
  term::TermEvent e;
  e.type = term::TermEvent::Type::END;
  push(e);
}

void TestBackend::wake() {
  char c = 'w';
  ssize_t n = ::write(wake_[1], &c, 1);
  (void)n;
}

void TestBackend::present(const CellBuffer & frame, const term::CursorRequest & cursor, const std::vector<ImagePlacement> & images,
                           const FrameInfo * info) {
  frame_ = frame;
  cursor_ = cursor;
  images_ = images;
  info_ = info ? *info : FrameInfo();
  frames_++;
}

}  // namespace cellui::test
