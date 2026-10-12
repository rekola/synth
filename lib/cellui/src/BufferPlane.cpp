#include <cellui/BufferPlane.h>

namespace cellui {

BufferPlane::BufferPlane(CellBuffer & buffer, const StyleProvider & styles, std::shared_ptr<void> app_context)
  : UIPlane(std::move(app_context), styles), buffer_(buffer) {
  setDim(std::pair(buffer.rows(), buffer.cols()));
  setPosition(std::pair(0, 0));
}

BufferPlane::BufferPlane(const BufferPlane & parent, int rows, int cols)
  : UIPlane(parent.getAppContext(), parent.getStyles()), buffer_(parent.buffer_), parent_(&parent) {
  setDim(std::pair(rows, cols));
  setPosition(std::pair(0, 0));
}

int BufferPlane::originRow() const { return (parent_ ? parent_->originRow() : 0) + getPosition().first; }
int BufferPlane::originCol() const { return (parent_ ? parent_->originCol() : 0) + getPosition().second; }
bool BufferPlane::shown() const { return visible_ && (!parent_ || parent_->shown()); }

void BufferPlane::erase() {
  if (shown()) buffer_.clear(originRow(), originCol(), getDim().first, getDim().second);
}

void BufferPlane::putstr(int y, int x, const std::string & s) {
  if (!shown() || y < 0 || y >= getDim().first || x >= getDim().second) return;
  int row = originRow(), col = originCol();
  buffer_.write(row + y, col + x, s, style_, row, col, getDim().first, getDim().second);
}

std::unique_ptr<UIPlane> BufferPlane::createChild() {
  return std::unique_ptr<UIPlane>(new BufferPlane(*this, getDim().first, getDim().second));
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

}  // namespace cellui
