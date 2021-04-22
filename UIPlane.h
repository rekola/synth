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

  virtual void move(int y, int x) = 0;
  virtual void resize(int rows, int cols) = 0;
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
  
  virtual std::pair<int, int> getDim() const = 0;

  std::shared_ptr<Controller> & getController() { return controller; }
  
private:
  std::shared_ptr<Controller> controller;
};

#endif
