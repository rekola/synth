#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"
#include "StyleProvider.h"
#include "../playback/Event.h"
#include "../util/Logger.h"
#include "../audio/AudioBuffer.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <set>

class UIMenu;
class Chart;
class HeatmapChart;
class InfoLine;
class StatusLine;
class PatternEditor;
class PatternMatrix;
class CoverArt;
class SpinBox;
class UIElement;
class AudioAPI;
class LaunchpadIO;
class LaunchpadManager;
class Song;
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

  // Read-only counterpart to executeCommand(), same active-element-then-
  // pattern-editor-then-own chain, but collecting every match instead of
  // stopping at the first hit - StatusLine's M-x autocomplete needs the
  // whole candidate set, not just whichever registry answers first. A set
  // (not a list) since more than one of those registries could define the
  // same name.
  std::set<std::string> commandCompletions(std::string_view prefix) const;

  void handlePlaybackEvent(PlaybackEvent & ev) override;
  void handleLogEvent(LogEvent & ev) override;
  void handleRecordEvent(RecordEvent & ev) override;
  void handleMidiEvent(MidiEvent & ev) override;
  void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) override;
  void handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) override;
  void handleLaunchpadChannelPressureEvent(LaunchpadChannelPressureEvent & ev) override;
  void handleVisualizationResultEvent(VisualizationResultEvent & ev) override;

protected:
  virtual void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) = 0;

  void initialize();
  void layout();
  bool renderComponents(bool refresh = false);
  // Shared by pattern_matrix_'s own Enter commit and a Launchpad "assign"
  // pad press in GridMode::SESSION (see UI::initialize()/UI::start() for
  // how each is wired to this) - one implementation of "commit this
  // (track, scene) cell", not two competing ones. See PatternMatrix.h's
  // own comment on why PatternMatrix itself never calls this directly.
  void commitOverviewCell(int track_id, int scene_idx);
  // Shared by PatternEditor's own leftward "nowhere further to go" edge
  // and Launchpad's equivalent "prev-track already at track 0" one (see
  // PatternEditor::setOverviewRequestCallback()/LaunchpadManager::
  // setSessionRequestCallback(), wired to this in UI::initialize()/
  // UI::start()) - moves focus to pattern_matrix_, which (via
  // renderComponents()'s own active_element_ check) is what actually puts
  // every connected Launchpad into GridMode::SESSION too.
  void requestOverviewFocus();
  // The reverse edge: leaves the overview, focusing pattern_editor_ on its
  // first track - both entry points (Launchpad's "next-track" already in
  // GridMode::SESSION, PatternMatrix's own rightward exit past its last
  // column - see LaunchpadManager::setSessionExitCallback()/
  // PatternMatrix::setExitRightCallback(), wired in UI::initialize()/
  // UI::start()) land here, so there's only one exit destination to reason
  // about, not two competing ones.
  void exitOverview();
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
  // Always visible in the scope row's leftmost columns (see UI::layout()),
  // sharing that row's real estate with chart_/heatmap_/volume_meter_.
  std::shared_ptr<PatternMatrix> pattern_matrix_;
  // Square cover-art thumbnail, sharing the scope row immediately to the
  // right of pattern_matrix_ - see CoverArt.h and UI::layout().
  std::shared_ptr<CoverArt> cover_art_;
  // The global octave stepper - see SpinBox.h and Controller::
  // getGlobalOctave(). Lives inline in the info bar's own row (see
  // layout()), always-on and click-activatable like pattern_editor_.
  std::shared_ptr<SpinBox> octave_control_;
  std::weak_ptr<UIElement> active_element_;

  // Set once at startup (see start()) - the Launchpad command-dispatch
  // path (handleLaunchpadButtonEvent) needs this directly (device-state
  // toggles, per-device command resolution); PatternEditor's own copy is
  // separate and only used for actual pattern editing (note entry).
  LaunchpadManager * launchpad_manager_ = nullptr;
};

#endif
