#ifndef _UI_H_
#define _UI_H_

#include "UIBase.h"

#include <ncpp/NotCurses.hh>

class Synth;
class AudioAPI;

class UI : public UIBase {
 public:
  UI() { }
  ~UI();
  
  void initialize();
  void start(Synth & synth, AudioAPI & audio);

  void setStatus(const std::string & s) override;

protected:
  void readInput(Synth & synth);
  
private:
  // struct notcurses * nc = 0;
  std::shared_ptr<ncpp::NotCurses> nc;
  std::shared_ptr<ncpp::Plane> top_line;
  std::shared_ptr<ncpp::Plane> status_line;

  bool close_ui = false;
};

#endif
