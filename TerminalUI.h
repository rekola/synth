#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"

#include <ncpp/NotCurses.hh>
#include <memory>

namespace ncpp {
  class NotCurses;
};

class TerminalUI : public UI {
 public:
  explicit TerminalUI() { }
  ~TerminalUI() { }
  
  void initialize(std::shared_ptr<Controller> & controller);

  void refresh() override;
  void render() override;

protected:
  void startUI() override;
  bool readInput();

private:
  std::unique_ptr<ncpp::NotCurses> nc;
};

#endif
