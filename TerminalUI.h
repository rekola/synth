#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include <memory>

namespace ncpp {
  class NotCurses;
};

class TerminalUI : public UI {
 public:
  explicit TerminalUI(std::shared_ptr<ncpp::NotCurses> _nc) : nc(_nc) { }
  ~TerminalUI() { }
  
  void initialize(std::shared_ptr<Controller> & controller);

  void refresh() override;
  void render() override;

protected:
  void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) override;
  bool readInput();

private:
  std::shared_ptr<ncpp::NotCurses> nc;
};

#endif
