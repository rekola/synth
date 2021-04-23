#ifndef _UIPLANE_H_
#define _UIPLANE_H_

#include <string>
#include <utility>
#include <memory>

class Controller;
class UIInput;

class UIPlane {
 public:
  UIPlane(const std::shared_ptr<Controller> & _controller) : controller(_controller) { }
  virtual ~UIPlane() { }

  virtual void resize(int rows, int cols) {
    setDim(std::pair(rows, cols));
  }
  virtual void move(int y, int x) {
    setPosition(std::pair(y, x));
  }
  virtual void setFgColor(int r, int g, int b) = 0;
  virtual void setBgColor(int r, int g, int b) = 0;
  virtual void erase() = 0;
  virtual void putstr(int y, int x, std::string s) = 0;
  virtual std::unique_ptr<UIPlane> createChild() = 0;
  virtual void drawBorder() = 0;
  virtual bool offerInput(const UIInput & input) = 0;
  virtual void showReader() = 0;
  virtual std::string closeReader() = 0;
  virtual bool readerActive() const = 0;
  virtual void showPicker() = 0;
  virtual void addItem(std::string id, std::string description) = 0;
  virtual void clearItems() = 0;

  const std::pair<int, int> & getPosition() const { return plane_pos; }
  const std::pair<int, int> & getDim() const { return plane_dim; }

  std::shared_ptr<Controller> & getController() { return controller; }

protected:
  void setDim(std::pair<int, int> dim) { plane_dim = dim; }
  void setPosition(std::pair<int, int> pos) { plane_pos = pos; }
  
private:
  std::shared_ptr<Controller> controller;
  std::pair<int, int> plane_dim, plane_pos;
};

#endif
