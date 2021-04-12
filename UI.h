#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"

#include <memory>
#include <string>

class Synth;

class UI : public UIElement {
 public:
  explicit UI() { }

  virtual void setStatus(const std::string & s) = 0;

  void setSynth(std::shared_ptr<Synth> & _synth) { synth = _synth; }
  std::shared_ptr<Synth> & getSynth() { return synth; }

private:
  std::shared_ptr<Synth> synth;
};

#endif
