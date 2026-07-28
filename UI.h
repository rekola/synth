#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"
#include "StyleProvider.h"
#include "Event.h"
#include "Logger.h"
#include "SampleData.h"

#include <memory>
#include <string>
#include <string_view>

class UIMenu;
class Chart;
class HeatmapChart;
class InfoLine;
class StatusLine;
class PatternEditor;
class HierarchyView;
class UIElement;
class AudioAPI;
class LaunchpadIO;
class LaunchpadManager;
class UI;

class StatusLogger : public Logger {
public:
  StatusLogger(UI * ui) : ui_(ui) { }

  void log(std::string s) override;

private:
  UI * ui_;
};

class UI : public UIElement {
 public:
  explicit UI() : logger_(this) { }

  virtual void refresh() = 0;
  virtual void render() = 0;

  void start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager);
  void setStatus(std::string s);

  bool offerInput(const InputEvent & input) override;

  // Checks the active element's registry before UI's own (mirrors Emacs
  // consulting the local keymap before the global one) - this is how
  // StatusLine's M-x path (routed via Controller::sendCommand's fallback,
  // see Controller.h) reaches per-widget commands like "set-mark".
  bool executeCommand(std::string_view name);

  void handlePlaybackEvent(PlaybackEvent & ev) override;
  void handleLogEvent(LogEvent & ev) override;
  void handleRecordEvent(RecordEvent & ev) override;
  void handleMidiEvent(MidiEvent & ev) override;
  void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) override;
  void handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) override;
  void handleVisualizationResultEvent(VisualizationResultEvent & ev) override;

protected:
  virtual void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) = 0;

  void initialize();
  void layout();
  bool renderComponents(bool refresh = false);
  bool tryActivate(int y, int x, std::shared_ptr<UIElement> element);
  Logger & getLogger() { return logger_; }
  
  std::shared_ptr<UIMenu> menu_;
  std::shared_ptr<Chart> chart_, volume_meter_;
  std::shared_ptr<HeatmapChart> heatmap_;
  // volume_meter_'s fixed domain size: 9 columns x 2 samples/braille-cell =
  // 18 - exactly order-3 ambisonic (16) + AuxA/AuxB (2), the largest
  // config this engine supports (AmbisonicEncoding.h's kAmbisonicOrder) -
  // so every channel is always on-screen at every order, never truncated.
  // Always filled in full every update (see handlePlaybackEvent())
  // regardless of the current config's real channel count, matching
  // displayFFT()'s own always-fill-the-whole-domain contract for the same
  // Chart widget class.
  static constexpr size_t kMaxMeterChannels = 18;
  std::shared_ptr<StatusLine> status_line_;
    
  bool close_ui_ = false;
  StyleProvider styles_;

  // Auto-scaling brightness reference for the DirAC heatmap - see
  // handleVisualizationResultEvent()'s own comment on why this needs to
  // persist across events rather than being derived fresh each time.
  float dirac_running_max_ = 0.0f;

private:  
  StatusLogger logger_;

  std::shared_ptr<InfoLine> info_line_;
  std::shared_ptr<PatternEditor> pattern_editor_;
  std::weak_ptr<UIElement> active_element_;

  // Set once at startup (see start()) - the Launchpad command-dispatch
  // path (handleLaunchpadButtonEvent) needs this directly (device-state
  // toggles, per-device command resolution); PatternEditor's own copy is
  // separate and only used for actual pattern editing (note entry).
  LaunchpadManager * launchpad_manager_ = nullptr;

  std::vector<std::shared_ptr<UIElement>> windows_;
};

#endif
