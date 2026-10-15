#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "ChoiceDialog.h"
#include "InfoDialog.h"
#include "../UI.h"
#include "../StyleProvider.h"
#include "../../playback/InputEvent.h"
#include <cellui/KeyReader.h>
#include <cellui/Scrim.h>
#include <cellui/Scopes.h>
#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include <cellui/BufferPlane.h>
#include <cellui/CellBuffer.h>

namespace cellui {
class Backend;
}

#include "../UIMenu.h"
#include "SpectrumMeter.h"
#include "../HeatmapChart.h"
#include "ChannelMeter.h"
class InfoLine;
class StatusLine;
class PatternEditor;
class ArrangementGrid;
class ClipGrid;
class OutlineView;
#include "CoverArt.h"
#include "SpinBox.h"

class TerminalUI : public UI {
 public:
  // Draws on, and reads from, `backend`: a terminal session, or a
  // TestBackend in a test.
  explicit TerminalUI(cellui::Backend & backend);
  ~TerminalUI();

  void initialize(std::shared_ptr<Controller> & controller);

  void refresh() override;
  void clearLastRow();
  void applyTheme(const Theme & theme) override;
  void render() override;

  bool offerInput(const cellui::Input & input) override;

  void setStatus(std::string s) override;
  void showInfoDialog(const std::string & title, const std::string & markdown) override;
  void showChoiceDialog(const std::string & title, std::vector<Choice> choices, int current,
                        std::function<void(int)> on_choose) override;

  // Checks the active element's registry before UI's own (mirrors Emacs
  // consulting the local keymap before the global one) - this is how
  // StatusLine's M-x path (routed via Controller::sendCommand's fallback,
  // see Controller.h) reaches per-widget commands like "set-mark".
  bool executeCommand(std::string_view name);

  // One turn of the main loop without waiting, for tests: takes the input
  // and the controller's events that are there, lays out, and presents a
  // frame (always, changed or not).
  void step();

  // Read-only counterpart to executeCommand(), same active-element-then-
  // pattern-editor-then-own chain, but collecting every match instead of
  // stopping at the first hit - StatusLine's M-x autocomplete needs the
  // whole candidate set, not just whichever registry answers first. A set
  // (not a list) since more than one of those registries could define the
  // same name.
  std::set<std::string> commandCompletions(std::string_view prefix) const;

  void handlePlaybackEvent(PlaybackEvent & ev) override;
  void handleLogEvent(LogEvent & ev) override;
  void handleMidiEvent(MidiEvent & ev) override;
  // handleLaunchpadChannelPressureEvent() is UI's own now - a pure
  // LaunchpadManager passthrough, with no widget dependency to override
  // here.
  void handleVisualizationResultEvent(VisualizationResultEvent & ev) override;

protected:
  void startUI(AudioAPI & audio, MidiInput & midi, LaunchpadIO & launchpad_io) override;
  int launchpadEditStepSize() const override;
  bool executeLaunchpadCommand(std::string_view name) override { return executeCommand(name); }
  void wireLaunchpad(LaunchpadManager & launchpad_manager) override;
  void viewChanged() override;
  bool readInput();
  // Which widget takes the keys, by name (for a test backend's frames).
  std::string focusName() const;

  // The "ESC-" indicator while an Escape waits for its next key.
  int escapeIndicatorPollTimeoutMs() const;
  void updateEscapeIndicator();

  // Constructs every widget (menu_ through octave_control_ below) and
  // defines every UI-level command/keybinding - called once from
  // initialize(), after the backend-specific widgets (menu_/chart_/
  // volume_meter_/heatmap_) it also depends on are already in place.
  void initializeWidgets();

  void layout();
  bool renderComponents(bool refresh = false);
  // Live View: keeps the clip grid and the pattern editor on the same
  // scene and track, whichever of them moved, and re-lays out when the
  // clip grid's height changes with the scene count. Returns whether the
  // layout changed.
  bool syncLiveView();
  // Shared by arrangement_grid_'s own Enter commit and a Launchpad "assign"
  // pad press in GridMode::LIVE (see wireLaunchpad()/initializeWidgets()
  // for how each is wired to this) - one implementation of "commit this
  // (track, row) cell", not two competing ones. See
  // ArrangementGrid.h's own comment on why ArrangementGrid itself never
  // calls this directly.
  void commitOverviewCell(int track_id, int row);
  bool tryActivate(int y, int x, std::shared_ptr<cellui::Element> element);
  // The widgets the current view shows that can take focus, in C-x o
  // (other-window) order.
  std::vector<std::shared_ptr<cellui::Element>> focusableElements() const;

private:
 cellui::Backend & backend_;
 // What the planes compose into, the size of the terminal; the root plane
 // covers it.
 cellui::CellBuffer screen_;
 cellui::BufferPlane * root_plane_ = nullptr;
 // Sizes the screen and the root plane to the terminal.
 void resizeScreen();

 std::shared_ptr<UIMenu> menu_;
 std::shared_ptr<SpectrumMeter> chart_;
 std::shared_ptr<ChannelMeter> volume_meter_;
 std::shared_ptr<HeatmapChart> heatmap_;
 // The scopes in the versions this terminal shows.
 std::unique_ptr<cellui::Scopes> scopes_;
 std::shared_ptr<StatusLine> status_line_;

 StyleProvider styles_;

 // Auto-scaling brightness reference for the DirAC heatmap - see
 // handleVisualizationResultEvent()'s own comment on why this needs to
 // persist across events rather than being derived fresh each time.
 float dirac_running_max_ = 0.0f;
 // What the scopes last drew, and when anything last moved: a silent,
 // unchanged display needs no redraw (a pixel blit is the costly part).
 std::vector<float> last_fft_, last_loudness_;
 std::vector<int> last_brightness_, last_saturation_;
 std::vector<uint64_t> last_playback_parts_; // SYNTH_UI_STATS only
 uint64_t last_playback_signature_ = 0;      // of the last playback snapshot that was drawn

 std::shared_ptr<InfoLine> info_line_;
 std::shared_ptr<PatternEditor> pattern_editor_;
 // Always visible in the scope row's leftmost columns (see layout()),
 // sharing that row's real estate with chart_/heatmap_/volume_meter_.
 std::shared_ptr<ArrangementGrid> arrangement_grid_;
 // Shown only in Live View (UI::View), above pattern_editor_, with
 // outline_view_ as an optional panel on its left (see layout()/
 // renderComponents()) - both stay real, constructed objects the whole
 // time; only which ones are on screen changes.
 std::shared_ptr<ClipGrid> clip_grid_;
 std::shared_ptr<OutlineView> outline_view_;
 // The modal dialog showInfoDialog() opens (About, ...); it takes all
 // input until closed. Its source is kept to re-center it on resize.
 InfoDialog info_dialog_;
 std::string info_dialog_title_, info_dialog_markdown_;
 void layoutInfoDialog();

 // The modal list showChoiceDialog() opens (the device pickers, ...); like
 // the info dialog it takes all input until it is chosen from or cancelled.
 ChoiceDialog choice_dialog_;
 std::function<void(int)> choice_callback_;
 void layoutChoiceDialog();
 void handleChoiceDialogInput(const cellui::Input & input);
 void closeChoiceDialog() {
   choice_dialog_.close();
   choice_callback_ = nullptr;
   layoutModalScrim();
   force_next_render_ = true;
  }
  void closeInfoDialog() {
    info_dialog_.close();
    info_dialog_title_.clear();
    layoutModalScrim();
    force_next_render_ = true;
  }
  // Under whichever modal dialog is open, the rest of the screen shows
  // through the theme's modal backdrop.
  cellui::Scrim modal_scrim_;
  void layoutModalScrim();
  void raiseOverlays();
  // Arrangement view's scope row (toggle-scopes); Live View never
  // shows it - see layout().
  bool scopes_visible_ = true;
  // Whether the spectrum/heatmap are on screen, as of the last layout().
  bool scopes_on_screen_ = true;
  // Set when the scopes come back on screen: what they hold is from before
  // they were hidden, and a silent analysis sends nothing more to replace
  // it, so the next result starts them empty.
  bool scopes_stale_ = false;
  std::string last_meter_label_; // the legend of the last result, for blanking
  void blankScopes();
  // What syncLiveView() last left both widgets showing.
  int synced_track_id_ = -1, laid_out_clip_grid_height_ = -1;
  // Set by a handler that changes what's on screen (the view changing,
  // Key::Resize) from *inside* input handling - before this
  // class's own main loop (startUI()) reaches its own renderComponents()
  // call, the one that actually decides whether to call render() (the
  // real terminal update). Calling renderComponents(true) directly from in
  // here would draw everything correctly into the planes, but its return
  // value (and every widget's own now-freshly-updated "did I already draw
  // this" memoized state) would be consumed right then and discarded - by
  // the time the outer loop makes its own renderComponents() call a moment
  // later, every widget correctly reports "nothing new to draw," so
  // render() never actually runs and the content never reaches the
  // terminal.
  // Setting this instead defers the forced refresh to that one outer
  // call, so its own return value (and therefore the flush) reflects it.
  bool force_next_render_ = false;
  // Square cover-art thumbnail, sharing the scope row immediately to the
  // right of arrangement_grid_ - see CoverArt.h and layout().
  std::shared_ptr<CoverArt> cover_art_;
  // The global octave stepper - see SpinBox.h and Controller::
  // getGlobalOctave(). Lives inline in the info bar's own row (see
  // layout()), always-on and click-activatable like pattern_editor_.
  std::shared_ptr<SpinBox> octave_control_;
  std::weak_ptr<cellui::Element> active_element_;

  // launchpad_manager_ itself is UI's own now (set once in UI::start(),
  // before wireLaunchpad() runs) - the Launchpad command-dispatch path
  // (handleLaunchpadButtonEvent below) still reads it directly (device-
  // state toggles, per-device command resolution); PatternEditor's own
  // copy is separate and only used for actual pattern editing (note
  // entry).

  // The terminal's inputs as the widgets see them, Escape as Meta included.
  cellui::KeyReader key_reader_;
  // Whether "ESC-" is on the status line for the current wait.
  bool escape_indicator_shown_ = false;
};

#endif
