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
#include <ncpp/Plane.hh>
#include <ncpp/Plot.hh>
#include <ncpp/Reader.hh>
#include <ncpp/Menu.hh>

#include <memory>

class AudioAPI;

class TerminalUI : public UI {
 public:
  explicit TerminalUI() { }
  ~TerminalUI() { }
  
  void initialize();
  void start(AudioAPI & audio);

  void setStatus(const std::string & s) override;
  bool offerInput(const UIInput & input) override;

protected:
  void layout();
  bool readInput();
  
private:
  std::shared_ptr<ncpp::NotCurses> nc;

  std::shared_ptr<UIMenu> menu;
  std::shared_ptr<Chart> left_chart, right_chart;
  std::shared_ptr<InfoLine> info_line;
  std::shared_ptr<StatusLine> status_line;
  std::shared_ptr<ScoreDisplay> score_display;
  
  SampleData waiting_data;

  bool close_ui = false;
};

#endif
