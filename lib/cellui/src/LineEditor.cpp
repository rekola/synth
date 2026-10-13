#include <cellui/LineEditor.h>

#include <cellui/Utf8.h>

namespace cellui {

std::string & LineEditor::killed() {
  static std::string text;
  return text;
}

void LineEditor::setText(const std::string & text) {
  clusters_ = utf8::clusters(text);
  cursor_ = clusters_.size();
  scroll_ = 0;
}

std::string LineEditor::text() const {
  std::string out;
  for (const auto & c : clusters_) out += c;
  return out;
}

void LineEditor::insert(const std::string & text) {
  // Re-segment around the insertion, so a combining mark joins the cluster
  // before it.
  std::string before, after;
  for (size_t i = 0; i < cursor_; i++) before += clusters_[i];
  for (size_t i = cursor_; i < clusters_.size(); i++) after += clusters_[i];
  auto head = utf8::clusters(before + text);
  clusters_ = head;
  cursor_ = head.size();
  for (auto & c : utf8::clusters(after)) clusters_.push_back(c);
}

bool LineEditor::isWordCluster(size_t i) const {
  const std::string & c = clusters_[i];
  unsigned char b = static_cast<unsigned char>(c[0]);
  return b >= 0x80 || (b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '_';
}

size_t LineEditor::wordBackward(size_t from) const {
  size_t i = from;
  while (i > 0 && !isWordCluster(i - 1)) i--;
  while (i > 0 && isWordCluster(i - 1)) i--;
  return i;
}

size_t LineEditor::wordForward(size_t from) const {
  size_t i = from;
  while (i < clusters_.size() && !isWordCluster(i)) i++;
  while (i < clusters_.size() && isWordCluster(i)) i++;
  return i;
}

void LineEditor::kill(size_t from, size_t to) {
  if (from >= to) return;
  std::string text;
  for (size_t i = from; i < to; i++) text += clusters_[i];
  killed() = text;
  clusters_.erase(clusters_.begin() + static_cast<long>(from), clusters_.begin() + static_cast<long>(to));
  cursor_ = from;
}

bool LineEditor::offerInput(const Input & input) {
  if (input.getKind() == Input::Kind::RELEASE) return false;
  int id = input.getId();
  bool ctrl = input.hasCtrl(), alt = input.hasAlt() || input.hasMeta();
  size_t n = clusters_.size();
  if ((ctrl && id == 'b') || (!ctrl && !alt && id == NCKEY_LEFT)) {
    if (cursor_ > 0) cursor_--;
  } else if ((ctrl && id == 'f') || (!ctrl && !alt && id == NCKEY_RIGHT)) {
    if (cursor_ < n) cursor_++;
  } else if ((ctrl && id == 'a') || id == NCKEY_HOME) {
    cursor_ = 0;
  } else if ((ctrl && id == 'e') || id == NCKEY_END) {
    cursor_ = n;
  } else if (alt && !ctrl && id == 'b') {
    cursor_ = wordBackward(cursor_);
  } else if (alt && !ctrl && id == 'f') {
    cursor_ = wordForward(cursor_);
  } else if (alt && (id == NCKEY_BACKSPACE || id == 0x7f)) {
    kill(wordBackward(cursor_), cursor_);
  } else if (id == NCKEY_BACKSPACE || (ctrl && id == 'h')) {
    if (cursor_ > 0) {
      clusters_.erase(clusters_.begin() + static_cast<long>(cursor_ - 1));
      cursor_--;
    }
  } else if (id == NCKEY_DEL || (ctrl && id == 'd')) {
    if (cursor_ < n) clusters_.erase(clusters_.begin() + static_cast<long>(cursor_));
  } else if (ctrl && id == 'k') {
    kill(cursor_, n);
  } else if (ctrl && id == 'u') {
    kill(0, cursor_);
  } else if (ctrl && id == 'w') {
    kill(wordBackward(cursor_), cursor_);
  } else if (alt && !ctrl && id == 'd') {
    kill(cursor_, wordForward(cursor_));
  } else if (ctrl && id == 'y') {
    insert(killed());
  } else if (ctrl && id == 't') {
    if (n >= 2 && cursor_ > 0) {
      size_t at = cursor_ == n ? n - 1 : cursor_;
      std::swap(clusters_[at - 1], clusters_[at]);
      cursor_ = at + 1;
    }
  } else if (!ctrl && !alt && id >= 0x20 && id != 0x7f && id < PRETERUNICODEBASE) {
    insert(utf8::encodeCodepoint(static_cast<uint32_t>(input.getTrueCaseId())));
  } else {
    return false;
  }
  return true;
}

int LineEditor::width(size_t from, size_t to) const {
  int w = 0;
  for (size_t i = from; i < to && i < clusters_.size(); i++) w += std::max(0, utf8::displayWidth(clusters_[i]));
  return w;
}

LineEditor::View LineEditor::view(int field_width) {
  View v;
  if (field_width <= 0) return v;
  // Keep one column of margin for the cursor at the right edge.
  int usable = std::max(1, field_width - 1);
  if (cursor_ < scroll_) scroll_ = cursor_;
  while (scroll_ < cursor_ && width(scroll_, cursor_) > usable) scroll_++;
  if (scroll_ > clusters_.size()) scroll_ = clusters_.size();
  v.cursor_column = width(scroll_, cursor_);
  int used = 0;
  for (size_t i = scroll_; i < clusters_.size(); i++) {
    int w = std::max(0, utf8::displayWidth(clusters_[i]));
    if (used + w > field_width) {
      if (used < field_width) v.text += std::string(static_cast<size_t>(field_width - used), ' ');
      break;
    }
    v.text += clusters_[i];
    used += w;
  }
  return v;
}

}  // namespace cellui
