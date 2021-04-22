#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"

#include "UIMenu.h"
#include "Chart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "ScoreDisplay.h"

#include <memory>
#include <string>

class UI : public UIElement {
 public:
  explicit UI() { }

  virtual void setStatus(const std::string & s) = 0;

protected:
  void layout();

  std::shared_ptr<UIMenu> menu;
  std::shared_ptr<Chart> chart, volume_meter;
  std::shared_ptr<InfoLine> info_line;
  std::shared_ptr<StatusLine> status_line;
  std::shared_ptr<ScoreDisplay> score_display;

  bool close_ui = false;

};

#endif
