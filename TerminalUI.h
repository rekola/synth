#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "SampleData.h"
#include "Logger.h"

#include <ncpp/NotCurses.hh>
#include <memory>

class AudioAPI;

namespace ncpp {
  class NotCurses;
};

class StatusLogger : public Logger {
public:
  StatusLogger(UI * _ui) : ui(_ui) { }

  void log(std::string s) override {
    ui->setStatus(s);
  }

private:
  UI * ui;
};

class TerminalUI : public UI {
 public:
  explicit TerminalUI() : logger(this) { }
  ~TerminalUI() { }
  
  void initialize(std::shared_ptr<Controller> & controller);
  void start(AudioAPI & audio);

  void refresh() override;
  void render() override;

  void handlePlaybackEvent(PlaybackEvent & ev);

protected:
  bool readInput();

private:
  std::unique_ptr<ncpp::NotCurses> nc;
  
  SampleData waiting_data;
  StatusLogger logger;
};

#endif
