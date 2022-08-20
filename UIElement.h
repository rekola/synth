#ifndef _UIELEMENT_H_
#define _UIELEMENT_H_

#include "EventHandler.h"
#include "UIPlane.h"

#include "UIColor.h"

#include <cstddef>
#include <memory>

class UIElement : public EventHandler {
 public:
  explicit UIElement() { }
  explicit UIElement(UIPlane & parent) {
    setPlane(parent.createChild());    
  }
  virtual ~UIElement() { }  

  virtual bool offerInput(const InputEvent & input) {
    if (plane_) {
      return plane_->offerInput(input);
    } else {
      return false;
    }
  }

  UIElement & putstr(int y, int x, const std::string & s) {
    if (plane_) plane_->putstr(y, x, s);
    return *this;
  }
  UIElement & putstr(int y, int x, char c) {
    if (plane_) {
      std::string s;
      s += c;
      plane_->putstr(y, x, s);
    }
    return *this;
  }
#if 0
  UIElement & putstrN(int y, int x, const std::string & s, int limit) {
    
  }
#endif
  UIElement & move(int y, int x) {
    if (plane_) plane_->move(y, x);
    return *this;
  }
  UIElement & resize(int rows, int cols) {
    if (plane_) plane_->resize(rows, cols);
    return *this;
  }
  UIElement & erase() {
    if (plane_) plane_->erase();
    return *this;
  }
  UIElement & fill() {
    if (plane_) {
      auto [rows, cols] = getDim();
      std::string s(cols, ' ');
      for (auto i = 0; i < rows; i++) plane_->putstr(i, 0, s);
    }
    return *this;
  }
  UIElement & setFgColor(int r, int g, int b) {
    if (plane_) plane_->setFgColor(r, g, b);
    return *this;
  }
  UIElement & setBgColor(int r, int g, int b) {
    if (plane_) plane_->setBgColor(r, g, b);
    return *this;
  }
  UIElement & setFgColor(UIColor color) {
    return setFgColor(color.getRed(), color.getGreen(), color.getBlue());
  }
  UIElement & setBgColor(UIColor color) {
    return setBgColor(color.getRed(), color.getGreen(), color.getBlue());
  }
  UIElement & setUnderline(bool b) {
    if (plane_) plane_->setUnderline(b);
    return *this;
  }

  std::pair<int, int> getPosition() const {
    if (plane_) return plane_->getPosition();
    else return std::pair(0, 0);
  }
  
  std::pair<int, int> getDim() const {
    if (plane_) return plane_->getDim();
    else return std::pair(0, 0);
  }
  
  Controller & getController() const { return *(plane_->getController()); }

protected:
  void setPlane(std::unique_ptr<UIPlane> plane) { plane_ = std::move(plane); }
  UIPlane & getPlane() { return *plane_; }
  
private:
  std::unique_ptr<UIPlane> plane_;
};

#endif
