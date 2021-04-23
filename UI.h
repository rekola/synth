#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"

class UIMenu;
class Chart;
class InfoLine;
class StatusLine;
class PatternEditor;
class InstrumentList;
class UIElement;

#include <memory>
#include <string>

class UI : public UIElement {
 public:
  explicit UI() { }

  virtual void setStatus(const std::string & s) = 0;
  virtual void refresh() = 0;

  bool offerInput(const UIInput & input);

protected:
  void initialize();
  void layout();
  bool tryActivate(int y, int x, std::shared_ptr<UIElement> element);

  std::shared_ptr<UIMenu> menu;
  std::shared_ptr<Chart> chart, volume_meter;
  std::shared_ptr<InfoLine> info_line;
  std::shared_ptr<StatusLine> status_line;
  std::shared_ptr<PatternEditor> pattern_editor;
  std::shared_ptr<InstrumentList> instrument_list;

  std::weak_ptr<UIElement> active_element;
  
  bool close_ui = false;
};

#endif
