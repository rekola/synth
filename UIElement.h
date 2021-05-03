#ifndef _UIELEMENT_H_
#define _UIELEMENT_H_

#include "UIPlane.h"

#include "UIColor.h"

#include <cstddef>
#include <memory>

class UIInput;

class UIElement {
 public:
  explicit UIElement() { }
  explicit UIElement(UIPlane & parent) {
    setPlane(parent.createChild());    
  }
  virtual ~UIElement() { }  

  virtual bool offerInput(const UIInput & input) {
    if (plane) {
      return plane->offerInput(input);
    } else {
      return false;
    }
  }

  UIElement & putstr(int y, int x, std::string s) {
    if (plane) plane->putstr(y, x, s);
    return *this;
  }
#if 0
  UIElement & putstrN(int y, int x, std::string s, size_t limit) {
    
  }
#endif
  UIElement & move(int y, int x) {
    if (plane) plane->move(y, x);
    return *this;
  }
  UIElement & resize(int rows, int cols) {
    if (plane) plane->resize(rows, cols);
    return *this;
  }
  UIElement & erase() {
    if (plane) plane->erase();
    return *this;
  }
  UIElement & fill() {
    if (plane) {
      auto [rows, cols] = getDim();
      std::string s;
      for (size_t i = 0; i < cols; i++) s += ' ';
      for (size_t i = 0; i < rows; i++) plane->putstr(i, 0, s);
    }
    return *this;
  }
  UIElement & setFgColor(int r, int g, int b) {
    if (plane) plane->setFgColor(r, g, b);
    return *this;
  }
  UIElement & setBgColor(int r, int g, int b) {
    if (plane) plane->setBgColor(r, g, b);
    return *this;
  }

  UIElement & setFgColor(UIColor color) {
    return setFgColor(color.getRed(), color.getGreen(), color.getBlue());
  }

  UIElement & setBgColor(UIColor color) {
    return setBgColor(color.getRed(), color.getGreen(), color.getBlue());
  }

  std::pair<int, int> getPosition() const {
    if (plane) return plane->getPosition();
    else return std::pair(0, 0);
  }
  
  std::pair<int, int> getDim() const {
    if (plane) return plane->getDim();
    else return std::pair(0, 0);
  }

  Controller & getController() const { return *(plane->getController()); }

protected:
  void setPlane(std::unique_ptr<UIPlane> _plane) { plane = std::move(_plane); }
  UIPlane & getPlane() { return *plane; }
  
private:
  std::unique_ptr<UIPlane> plane;
};

#endif
