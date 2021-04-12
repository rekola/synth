#ifndef _UIPLANE_H_
#define _UIPLANE_H_

#include <string>
#include <utility>
#include <memory>

class UIPlane {
 public:
  UIPlane() { }
  virtual ~UIPlane() { }

  virtual void move(int y, int x) = 0;
  virtual void resize(int rows, int cols) = 0;
  virtual void setFgColor(int r, int g, int b) = 0;
  virtual void setBgColor(int r, int g, int b) = 0;
  virtual void erase() = 0;
  virtual void putstr(int y, int x, std::string s) = 0;
  virtual std::unique_ptr<UIPlane> createChild() = 0;
  
  virtual std::pair<int, int> getDim() const = 0;
};

#endif
