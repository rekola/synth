#ifndef _TERMINALUI_H_
#define _TERMINALUI_H_

#include "InfoDialog.h"
#include "../UI.h"
#include "../StyleProvider.h"
#include "../../playback/InputEvent.h"
#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace ncpp {
  class NotCurses;
};

class UIMenu;
class SpectrumMeter;
class HeatmapChart;
class ChannelMeter;
class InfoLine;
class StatusLine;
class PatternEditor;
class ArrangementGrid;
class ClipGrid;
class OutlineView;
class CoverArt;
class SpinBox;

class TerminalUI : public UI {
 public:
  explicit TerminalUI(std::shared_ptr<ncpp::NotCurses> _nc);
  ~TerminalUI();

  void initialize(std::shared_ptr<Controller> & controller);

  void refresh() override;
  void render() override;

  bool offerInput(const InputEvent & input) override;

  void setStatus(std::string s) override;
  void showInfoDialog(const std::string & title, const std::string & markdown) override;

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
  void handleRecordingLatencyEvent(RecordingLatencyEvent & ev) override;
  void handleThresholdRecordingTriggeredEvent(ThresholdRecordingTriggeredEvent & ev) override;
  void handleMidiEvent(MidiEvent & ev) override;
  void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) override;
  void handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) override;
  // handleLaunchpadChannelPressureEvent() is UI's own now - a pure
  // LaunchpadManager passthrough, with no widget dependency to override
  // here.
  void handleVisualizationResultEvent(VisualizationResultEvent & ev) override;

protected:
  void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) override;
  void wireLaunchpad(LaunchpadManager & launchpad_manager) override;
  void viewChanged() override;
  bool readInput();

  // How long a pending Escape (EscapeSequenceCoalescer::escapePending())
  // must stay open before startUI()'s own loop shows the "ESC-" indicator
  // - see that loop's own comment on why this is a delay rather than
  // immediate, and updateEscapeIndicator()'s comment for what drives it.
  int escapeIndicatorPollTimeoutMs() const;
  void updateEscapeIndicator();

  // Constructs every widget (menu_ through octave_control_ below) and
  // defines every UI-level command/keybinding - called once from
  // initialize(), after the notcurses-specific widgets (menu_/chart_/
  // volume_meter_/heatmap_) it also depends on are already in place.
  void initializeWidgets();

  void layout();
  bool renderComponents(bool refresh = false);
  // Session view: keeps the clip grid and the pattern editor on the same
  // scene and track, whichever of them moved, and re-lays out when the
  // clip grid's height changes with the scene count. Returns whether the
  // layout changed.
  bool syncSessionView();
  // Shared by arrangement_grid_'s own Enter commit and a Launchpad "assign"
  // pad press in GridMode::SESSION (see wireLaunchpad()/initializeWidgets()
  // for how each is wired to this) - one implementation of "commit this
  // (track, row) cell", not two competing ones. See
  // ArrangementGrid.h's own comment on why ArrangementGrid itself never
  // calls this directly.
  void commitOverviewCell(int track_id, int row);
  bool tryActivate(int y, int x, std::shared_ptr<UIElement> element);
  // The widgets the current view shows that can take focus, in C-x o
  // (other-window) order.
  std::vector<std::shared_ptr<UIElement>> focusableElements() const;

private:
  std::shared_ptr<ncpp::NotCurses> nc;

  std::shared_ptr<UIMenu> menu_;
  std::shared_ptr<SpectrumMeter> chart_;
  std::shared_ptr<ChannelMeter> volume_meter_;
  std::shared_ptr<HeatmapChart> heatmap_;
  std::shared_ptr<StatusLine> status_line_;

  StyleProvider styles_;

  // Auto-scaling brightness reference for the DirAC heatmap - see
  // handleVisualizationResultEvent()'s own comment on why this needs to
  // persist across events rather than being derived fresh each time.
  float dirac_running_max_ = 0.0f;

  std::shared_ptr<InfoLine> info_line_;
  std::shared_ptr<PatternEditor> pattern_editor_;
  // Always visible in the scope row's leftmost columns (see layout()),
  // sharing that row's real estate with chart_/heatmap_/volume_meter_.
  std::shared_ptr<ArrangementGrid> arrangement_grid_;
  // Shown only in Session view (UI::View), above pattern_editor_, with
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
  void closeInfoDialog() { info_dialog_.close(); info_dialog_title_.clear(); force_next_render_ = true; }
  // Arrangement view's scope row (toggle-scopes); Session view never
  // shows it - see layout().
  bool scopes_visible_ = true;
  // What syncSessionView() last left both widgets showing.
  int synced_track_id_ = -1, laid_out_clip_grid_height_ = -1;
  // Set by a handler that changes what's on screen (the view changing,
  // NCKEY_RESIZE) from *inside* input handling - before this
  // class's own main loop (startUI()) reaches its own renderComponents()
  // call, the one that actually decides whether to call nc->render() (the
  // real terminal flush). Calling renderComponents(true) directly from in
  // here would draw everything correctly into notcurses's own plane
  // state, but its return value (and every widget's own now-freshly-
  // updated "did I already draw this" memoized state) would be consumed
  // right then and discarded - by the time the outer loop makes its own
  // renderComponents() call a moment later, every widget correctly
  // reports "nothing new to draw," so nc->render() never actually runs
  // and the correctly-drawn content never reaches the real terminal.
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
  std::weak_ptr<UIElement> active_element_;

  // launchpad_manager_ itself is UI's own now (set once in UI::start(),
  // before wireLaunchpad() runs) - the Launchpad command-dispatch path
  // (handleLaunchpadButtonEvent below) still reads it directly (device-
  // state toggles, per-device command resolution); PatternEditor's own
  // copy is separate and only used for actual pattern editing (note
  // entry).

  // Legacy xterm/VT220 "SS3 + modifier digit" escape sequence recognizer
  // for Ctrl+Numpad-Divide/Multiply (see readInput()'s own comment) - the
  // raw bytes buffered so far (ESC, then 'O', then '5') can be replayed
  // verbatim as ordinary InputEvents if what looked like the start of the
  // sequence turns out not to be (e.g. a real standalone Escape, or
  // Esc-then-x for M-x). Kept notcurses-type-free (plain ints/unsigned
  // rather than ncinput) so this header doesn't need a notcurses include.
  struct PendingRawKey { int id = 0, y = 0, x = 0; unsigned modifiers = 0; InputEvent::Kind kind = InputEvent::Kind::UNKNOWN; };
  int kp_escape_depth_ = 0; // 0 = no legacy KP-escape sequence in progress, 1-3 = that many bytes matched so far
  PendingRawKey kp_escape_pending_[3]; // ESC, 'O', '5', in order
  // The buffered ESC already went to a widget that wanted it bare (see
  // UIElement::wantsBareEscape()) - replaying it would deliver it twice.
  bool kp_escape_delivered_ = false;

  // Alt-key coalescing (EscapeCoalescer.h) - the notcurses input source
  // readInput() actually pulls events from. Kept behind a pointer to an
  // incomplete type, defined only in TerminalUI.cpp, for the same reason
  // kp_escape_pending_ above is kept plain-int typed: this header
  // shouldn't need a notcurses include.
  class EscapeInputPipeline;
  std::unique_ptr<EscapeInputPipeline> escape_input_;

  // When the currently-open Escape wait began (unset when none is open) -
  // set in readInput() the moment EscapeSequenceCoalescer::escapePending()
  // turns true, read by updateEscapeIndicator()/escapeIndicatorPollTimeoutMs()
  // to decide when the delay has elapsed. escape_indicator_shown_ tracks
  // whether "ESC-" has actually been printed yet for the current wait, so
  // it's only cleared (and only if it was actually shown) once, exactly
  // when the wait ends.
  std::optional<std::chrono::steady_clock::time_point> escape_pending_since_;
  bool escape_indicator_shown_ = false;
};

#endif
