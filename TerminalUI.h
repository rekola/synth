#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "UI.h"
#include "SampleData.h"

#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Plot.hh>
#include <ncpp/Reader.hh>
#include <ncpp/Menu.hh>

#include <memory>
#include <unordered_map>

class Synth;
class AudioAPI;

class TerminalUI : public UI {
 public:
  explicit TerminalUI();
  ~TerminalUI();
  
  void initialize();
  void start(Synth & synth, AudioAPI & audio);

  void setStatus(const std::string & s) override;

protected:
  void renderInfo(Synth & synth);
  void renderScore(Synth & synth);
  void readInput(Synth & synth);
  
private:
  std::shared_ptr<ncpp::NotCurses> nc;
  std::shared_ptr<ncpp::Menu> menu;
  std::shared_ptr<ncpp::Plane> top_line;
  std::shared_ptr<ncpp::Plane> left_plot_plane, right_plot_plane;
  std::shared_ptr<ncpp::PlotD> left_plot, right_plot;
  std::shared_ptr<ncpp::Plane> score_plane;
  std::shared_ptr<ncpp::Plane> info_line;
  std::shared_ptr<ncpp::Plane> status_line;
  std::shared_ptr<ncpp::Reader> reader;
  std::unordered_map<short, std::string> midi_note_names;
  
  SampleData waiting_data;

  bool close_ui = false;
};

#endif
