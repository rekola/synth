#include <cellui/BufferPlane.h>

#include <algorithm>

namespace cellui {

BufferPlane::BufferPlane(CellBuffer & target, const StyleProvider & styles, std::shared_ptr<void> app_context)
  : UIPlane(std::move(app_context), styles), local_(target.rows(), target.cols()), target_(&target) {
  setDim(std::pair(target.rows(), target.cols()));
  setPosition(std::pair(0, 0));
}

BufferPlane::BufferPlane(BufferPlane & parent)
  : UIPlane(parent.getAppContext(), parent.getStyles()),
    local_(parent.getDim().first, parent.getDim().second), parent_(&parent) {
  setDim(parent.getDim());
  setPosition(std::pair(0, 0));
  parent.children_.push_back(this);
}

BufferPlane::~BufferPlane() {
  for (BufferPlane * child : children_) child->parent_ = nullptr;
  if (parent_) parent_->children_.erase(std::remove(parent_->children_.begin(), parent_->children_.end(), this), parent_->children_.end());
}

void BufferPlane::resize(int rows, int cols) {
  rows = std::max(0, rows);
  cols = std::max(0, cols);
  CellBuffer resized(rows, cols);
  for (int r = 0; r < std::min(rows, local_.rows()); r++) {
    for (int c = 0; c < std::min(cols, local_.cols()); c++) resized.put(r, c, local_.at(r, c));
  }
  local_ = std::move(resized);
  UIPlane::resize(rows, cols);
}

void BufferPlane::moveToTop() {
  if (!parent_) return;
  auto & siblings = parent_->children_;
  siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
  siblings.push_back(this);
}

void BufferPlane::putstr(int y, int x, const std::string & s) {
  local_.write(y, x, s, style_);
}

std::unique_ptr<UIPlane> BufferPlane::createChild() {
  return std::unique_ptr<UIPlane>(new BufferPlane(*this));
}

void BufferPlane::drawBorder() {
  auto [rows, cols] = getDim();
  if (rows < 2 || cols < 2) return;
  std::string horizontal;
  for (int c = 0; c < cols - 2; c++) horizontal += "─";
  putstr(0, 0, "╭" + horizontal + "╮");
  putstr(rows - 1, 0, "╰" + horizontal + "╯");
  for (int r = 1; r < rows - 1; r++) {
    putstr(r, 0, "│");
    putstr(r, cols - 1, "│");
  }
}

void BufferPlane::refresh() {
  BufferPlane & top = root();
  if (!top.target_) return;
  top.target_->clear(0, 0, top.target_->rows(), top.target_->cols());
  top.compose(*top.target_, 0, 0, 0, 0, top.target_->rows(), top.target_->cols());
}

void BufferPlane::compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols) const {
  if (!visible_) return;
  // The plane's own rectangle, inside the clip.
  int row_lo = std::max(clip_row, row), row_hi = std::min(clip_row + clip_rows, row + local_.rows());
  int col_lo = std::max(clip_col, col), col_hi = std::min(clip_col + clip_cols, col + local_.cols());
  for (int r = row_lo; r < row_hi; r++) {
    for (int c = col_lo; c < col_hi; c++) {
      const CellBuffer::Cell & cell = local_.at(r - row, c - col);
      if (CellBuffer::isBlank(cell)) continue;
      // A double-width character is drawn whole or not at all.
      if (cell.continuation && c - 1 < col_lo) continue;
      if (c + 1 < col + local_.cols() && local_.at(r - row, c + 1 - col).continuation && c + 1 >= col_hi) continue;
      out.put(r, c, cell);
    }
  }
  if (row_lo >= row_hi || col_lo >= col_hi) return;
  for (const BufferPlane * child : children_) {
    child->compose(out, row + child->getPosition().first, col + child->getPosition().second,
                   row_lo, col_lo, row_hi - row_lo, col_hi - col_lo);
  }
}

}  // namespace cellui
