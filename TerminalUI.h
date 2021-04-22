#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "Chart.h"
#include "ScoreDisplay.h"
#include "SampleData.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "UIMenu.h"

#include <ncpp/NotCurses.hh>

#include <memory>

class AudioAPI;

class TerminalUI : public UI {
 public:
  explicit TerminalUI() { }
  ~TerminalUI() { }
  
  void initialize(std::shared_ptr<Controller> & controller);
  void start(AudioAPI & audio);

  void setStatus(const std::string & s) override;
  bool offerInput(const UIInput & input) override;

protected:
  bool readInput();

private:
  std::shared_ptr<ncpp::NotCurses> nc;
  
  SampleData waiting_data;
};

#endif
