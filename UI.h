#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"
#include "StyleProvider.h"
#include "Event.h"
#include "Logger.h"

#include <memory>
#include <string>

class UIMenu;
class Chart;
class InfoLine;
class StatusLine;
class PatternEditor;
class InstrumentList;
class UIElement;
class AudioAPI;
class UI;

class StatusLogger : public Logger {
public:
  StatusLogger(UI * _ui) : ui(_ui) { }

  void log(std::string s) override;

private:
  UI * ui;
};

class UI : public UIElement {
 public:
  explicit UI() : logger(this) { }

  virtual void refresh() = 0;
  virtual void render() = 0;

  void start(AudioAPI & audio); 
  void setStatus(std::string s);
  
  bool offerInput(const UIInput & input) override;

  void handlePlaybackEvent(PlaybackEvent & ev) override;
  void handleLogEvent(LogEvent & ev) override;
  void handleRecordEvent(RecordEvent & ev) override;

protected:
  virtual void startUI() = 0;

  void initialize();
  void layout();
  bool renderComponents(bool refresh = false);
  bool tryActivate(int y, int x, std::shared_ptr<UIElement> element);
  
  std::shared_ptr<UIMenu> menu;
  std::shared_ptr<Chart> chart, volume_meter;
  std::shared_ptr<StatusLine> status_line;
    
  bool close_ui = false;
  bool is_recording = false;
  StyleProvider styles;  

private:
  std::shared_ptr<InfoLine> info_line;
  std::shared_ptr<PatternEditor> pattern_editor;
  std::shared_ptr<InstrumentList> instrument_list;

  std::weak_ptr<UIElement> active_element;

  StatusLogger logger;
};

#endif
