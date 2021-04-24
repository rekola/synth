#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "SampleData.h"

#include <ncpp/NotCurses.hh>
#include <memory>

class AudioAPI;

namespace ncpp {
  class NotCurses;
};

class TerminalUI : public UI {
 public:
  explicit TerminalUI() { }
  ~TerminalUI() { }
  
  void initialize(std::shared_ptr<Controller> & controller);
  void start(AudioAPI & audio);

  void refresh() override;
  void render() override;

protected:
  bool readInput();

private:
  std::unique_ptr<ncpp::NotCurses> nc;
  
  SampleData waiting_data;
};

#endif
