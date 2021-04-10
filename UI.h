#ifndef _UI_H_
#define _UI_H_

#include "UIBase.h"
#include "SampleData.h"

#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Plot.hh>
#include <ncpp/Reader.hh>
#include <ncpp/Menu.hh>

#include <memory>

class Synth;
class AudioAPI;

class UI : public UIBase {
 public:
  UI() { }
  ~UI();
  
  void initialize();
  void start(Synth & synth, AudioAPI & audio);

  void setStatus(const std::string & s) override;

protected:
  void renderInfo(Synth & synth);
  void renderScore(Synth & synth);
  void readInput(Synth & synth);
  
private:
  // struct notcurses * nc = 0;
  std::shared_ptr<ncpp::NotCurses> nc;
  std::shared_ptr<ncpp::Menu> menu;
  std::shared_ptr<ncpp::Plane> top_line;
  std::shared_ptr<ncpp::Plane> left_plot_plane, right_plot_plane;
  std::shared_ptr<ncpp::PlotD> left_plot, right_plot;
  std::shared_ptr<ncpp::Plane> score_plane;
  std::shared_ptr<ncpp::Plane> info_line;
  std::shared_ptr<ncpp::Plane> status_line;
  std::shared_ptr<ncpp::Reader> reader;

  SampleData waiting_data;

  bool close_ui = false;
};

#endif
