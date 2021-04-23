#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "SampleData.h"

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

  void setStatus(const std::string & s) override;
  void refresh() override;

protected:
  bool readInput();

private:
  std::shared_ptr<ncpp::NotCurses> nc;
  
  SampleData waiting_data;
};

#endif
