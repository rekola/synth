#include <cellui/term/TestTerminal.h>

#include <cellui/Utf8.h>

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace cellui::term {

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

// JSON string literal.
std::string quote(const std::string & s) {
  std::string out = "\"";
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char buf[8];
        snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out + "\"";
}

std::string hex(const Color & c) {
  char buf[8];
  snprintf(buf, sizeof buf, "#%02x%02x%02x", c.getRed() & 255, c.getGreen() & 255, c.getBlue() & 255);
  return buf;
}

bool sameStyle(const CellBuffer::Cell & a, const CellBuffer::Cell & b) {
  auto eq = [](const std::optional<Color> & x, const std::optional<Color> & y) {
    if (x.has_value() != y.has_value()) return false;
    return !x || (x->getRed() == y->getRed() && x->getGreen() == y->getGreen() && x->getBlue() == y->getBlue());
  };
  return eq(a.fg, b.fg) && eq(a.bg, b.bg) && a.bold == b.bold && a.italic == b.italic && a.underline == b.underline &&
         a.bg_blend == b.bg_blend && a.fg_contrast == b.fg_contrast && a.image == b.image;
}

std::string styleJson(const CellBuffer::Cell & c) {
  std::string s;
  auto add = [&](const std::string & field) { s += (s.empty() ? "" : ",") + field; };
  if (c.fg) add("\"fg\":\"" + hex(*c.fg) + "\"");
  if (c.bg) add("\"bg\":\"" + hex(*c.bg) + "\"");
  if (c.bold) add("\"bold\":true");
  if (c.italic) add("\"italic\":true");
  if (c.underline) add("\"underline\":true");
  if (c.bg_blend) add("\"bgBlend\":true");
  if (c.fg_contrast) add("\"fgContrast\":true");
  if (c.image) add("\"image\":" + std::to_string(c.image - 1));
  return s;
}

void planeJson(const PlaneInfo & p, std::string & out) {
  out += "{";
  if (!p.name.empty()) out += "\"name\":" + quote(p.name) + ",";
  out += "\"y\":" + std::to_string(p.y) + ",\"x\":" + std::to_string(p.x) + ",\"rows\":" + std::to_string(p.rows) +
         ",\"cols\":" + std::to_string(p.cols) + ",\"visible\":" + (p.visible ? "true" : "false");
  if (p.reader) out += ",\"reader\":true";
  if (p.picker) out += ",\"picker\":true";
  out += ",\"children\":[";
  for (size_t i = 0; i < p.children.size(); i++) {
    if (i) out += ",";
    planeJson(p.children[i], out);
  }
  out += "]}";
}

}  // namespace

TestTerminal::TestTerminal(int rows, int cols, Capabilities caps)
  : caps_(std::move(caps)), rows_(rows), cols_(cols), frame_(rows, cols) {
  caps_.rows = rows;
  caps_.cols = cols;
  makePipe(pipe_);
  makePipe(wake_);
}

TestTerminal::~TestTerminal() {
  for (int * fds : { pipe_, wake_ }) {
    closeFd(fds[0]);
    closeFd(fds[1]);
  }
}

std::vector<TermEvent> TestTerminal::poll() {
  drain(pipe_[0]);
  drain(wake_[0]);
  std::vector<TermEvent> out(queue_.begin(), queue_.end());
  queue_.clear();
  return out;
}

void TestTerminal::push(const TermEvent & event) {
  queue_.push_back(event);
  char c = 'e';
  ssize_t n = ::write(pipe_[1], &c, 1);
  (void)n;
}

void TestTerminal::pushText(const std::string & text) {
  for (const std::string & cluster : utf8::clusters(text)) {
    TermEvent e = TermEvent::makeKey(0);
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

void TestTerminal::pushMouse(int y, int x, int key, unsigned mods, TermEvent::Kind kind) {
  TermEvent e;
  e.type = TermEvent::Type::MOUSE;
  e.kind = kind;
  e.key = key;
  e.mods = mods;
  e.y = y;
  e.x = x;
  push(e);
}

void TestTerminal::pushPaste(const std::string & text) {
  TermEvent e;
  e.type = TermEvent::Type::PASTE;
  e.text = text;
  push(e);
}

void TestTerminal::resize(int rows, int cols) {
  rows_ = rows;
  cols_ = cols;
  caps_.rows = rows;
  caps_.cols = cols;
  TermEvent e;
  e.type = TermEvent::Type::RESIZE;
  e.rows = rows;
  e.cols = cols;
  push(e);
}

void TestTerminal::pushEnd() {
  TermEvent e;
  e.type = TermEvent::Type::END;
  push(e);
}

void TestTerminal::wake() {
  char c = 'w';
  ssize_t n = ::write(wake_[1], &c, 1);
  (void)n;
}

void TestTerminal::present(const CellBuffer & frame, const CursorRequest & cursor, const std::vector<ImagePlacement> & images,
                           const FrameInfo * info) {
  frame_ = frame;
  cursor_ = cursor;
  images_ = images;
  info_ = info ? *info : FrameInfo();
  frames_++;
}

std::string TestTerminal::toJson() const {
  std::string out = "{";
  out += "\"rows\":" + std::to_string(frame_.rows()) + ",\"cols\":" + std::to_string(frame_.cols());
  out += ",\"cursor\":";
  if (cursor_.visible) out += "{\"y\":" + std::to_string(cursor_.y) + ",\"x\":" + std::to_string(cursor_.x) + "}";
  else out += "null";
  out += ",\"focus\":" + quote(info_.focus);
  out += ",\"planes\":";
  planeJson(info_.planes, out);

  // Each row: its text, and the runs of cells that share a style (only the
  // runs that have one).
  out += ",\"lines\":[";
  for (int r = 0; r < frame_.rows(); r++) {
    if (r) out += ",";
    std::string text;
    std::string runs;
    int c = 0;
    while (c < frame_.cols()) {
      const auto & cell = frame_.at(r, c);
      int end = c + 1;
      while (end < frame_.cols() && sameStyle(cell, frame_.at(r, end))) end++;
      for (int i = c; i < end; i++) {
        if (!frame_.at(r, i).continuation) text += frame_.at(r, i).text;
      }
      std::string style = styleJson(cell);
      if (!style.empty()) {
        runs += (runs.empty() ? "" : ",") + std::string("{\"x\":") + std::to_string(c) + ",\"cols\":" + std::to_string(end - c) + "," + style + "}";
      }
      c = end;
    }
    // Trailing blanks are noise.
    while (!text.empty() && text.back() == ' ') text.pop_back();
    out += "{\"text\":" + quote(text) + ",\"styles\":[" + runs + "]}";
  }
  out += "],\"images\":[";
  for (size_t i = 0; i < images_.size(); i++) {
    const auto & p = images_[i];
    if (i) out += ",";
    out += "{\"row\":" + std::to_string(p.row) + ",\"col\":" + std::to_string(p.col);
    if (p.image) {
      out += ",\"width\":" + std::to_string(p.image->width) + ",\"height\":" + std::to_string(p.image->height);
    }
    out += "}";
  }
  out += "]}";
  return out;
}

}  // namespace cellui::term
