#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"
#include "../util/Logger.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class AudioAPI;
class LaunchpadIO;
class LaunchpadManager;
class LaunchpadChannelPressureEvent;
class LaunchpadPadEvent;
class LaunchpadButtonEvent;
class UI;

class StatusLogger : public Logger {
public:
  StatusLogger(UI * ui) : ui_(ui) { }

  void log(std::string s) override;

private:
  UI * ui_;
};

// Toolkit-agnostic top-level application UI - owns nothing widget-shaped
// itself (a concrete subclass, e.g. TerminalUI, owns its own widget set and
// screen layout), just the app-level lifecycle every backend needs:
// spawning the audio/visualization threads, running until told to quit,
// and reporting status. A future non-terminal UI would subclass this
// directly rather than TerminalUI.
class UI : public UIElement {
 public:
  explicit UI() : logger_(this) { initializeCommands(); }

  virtual void refresh() = 0;
  virtual void render() = 0;

  // Spawns the audio/visualization threads, hands off to startUI() (the
  // concrete backend's own main loop) until it returns, then signals both
  // threads to stop and joins them.
  void start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager);

  virtual void setStatus(std::string s) = 0;

  // Shows a modal dialog with Markdown content (see Markdown.h) - what a
  // command like "about" calls, so each backend only renders the text.
  virtual void showInfoDialog(const std::string & title, const std::string & markdown) = 0;

  // One entry of a choice dialog: what the person sees, and what the command
  // acts on.
  struct Choice {
    std::string label;
    std::string value;
  };
  // Shows a modal list to pick one entry from. `current` is the entry in
  // use, marked and where the selection starts (-1 for none). Calls
  // `on_choose` with the chosen index once, or never if the dialog is
  // cancelled. A backend with no such dialog says so on the status line.
  virtual void showChoiceDialog(const std::string & title, std::vector<Choice> choices, int current,
                                std::function<void(int)> on_choose) = 0;

  // Launchpad hardware input isn't tied to any one visual frontend, so its
  // event handling lives here rather than in a concrete backend - this one
  // is a pure passthrough to LaunchpadManager, with no widget dependency of
  // its own. Pad and button events are shared too; the two hooks below
  // (launchpadEditStepSize()/executeLaunchpadCommand()) are the only
  // backend-specific parts.
  void handleLaunchpadChannelPressureEvent(LaunchpadChannelPressureEvent & ev) override;
  // Sample recording (live mic takes) is pure Controller bookkeeping, so
  // every backend shares it.
  void handleRecordEvent(RecordEvent & ev) override;
  void handleRecordingLatencyEvent(RecordingLatencyEvent & ev) override;
  void handleThresholdRecordingTriggeredEvent(ThresholdRecordingTriggeredEvent & ev) override;
  void handleLaunchpadPadEvent(LaunchpadPadEvent & ev) override;
  void handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) override;

  // Which layout the active song is shown in - UI state, independent of
  // which buffer is active. Arrangement: the arrangement overview plus the
  // pattern editor. Session: the clip grid (with the optional outline
  // panel beside it) plus the pattern editor.
  enum class View { ARRANGEMENT, LIVE };
  View getView() const { return view_; }
  // The view the UI starts in (the --view option) - Session unless set
  // before the backend's initialize().
  void setInitialView(View view) { initial_view_ = view; }
  bool isOutlineVisible() const { return outline_visible_; }

protected:
  void setView(View view);
  void setOutlineVisible(bool visible);
  // Called after the view or the outline panel's visibility changes - a
  // backend re-lays out its widgets and moves focus here.
  virtual void viewChanged() { }

  virtual void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) = 0;

  // Hook for a concrete UI to wire up whatever per-widget Launchpad
  // callbacks it needs (session/track-move, drum-edit request, ...) -
  // default no-op, since a UI backend with no Launchpad-aware widgets of
  // its own needs none of them. launchpad_manager_ is already set (see
  // start()) by the time this runs.
  virtual void wireLaunchpad(LaunchpadManager & launchpad_manager) { }

  // Rows a Launchpad note-entry press advances the edit position by.
  virtual int launchpadEditStepSize() const { return 1; }
  // Runs a command named by a Launchpad button; a backend overrides this to
  // route it through its own focus-aware dispatch.
  virtual bool executeLaunchpadCommand(std::string_view name) { return executeCommand(name); }

  // Song::getCurrentTrackId()'s own id, converted to whichever index-space
  // `track_ids` uses - every LaunchpadManager call still takes a plain
  // index (a real per-list position, needed for arithmetic like "move one
  // track over" and for auto-growing a brand-new song up to a target
  // count), just no longer sourced from any one widget's own cursor. Falls
  // back to 0 (not -1) when the id isn't found (unset, or a track this
  // list doesn't include) - the same "just pick the first one" fallback
  // PatternEditor's own cursor starts at.
  static int indexOfTrack(const std::vector<int> & track_ids, int track_id);

  Logger & getLogger() { return logger_; }

  // Device selection shared by every backend; each runs the choice through
  // the Controller (which persists it) and tells the user what happened on
  // the status line. `name`/`spec` are DeviceSettings values; `label` is
  // what the user picked it by.
  void selectCaptureDevice(const std::string & name, const std::string & label);
  void selectPlaybackDevice(const std::string & name, const std::string & label);
  // MIDI is read on the UI thread, so this connects it directly.
  void selectMidiInput(const std::string & spec, const std::string & label);

  // True once a backend's main loop should end: close_ui_ was set, or a
  // shutdown signal (SIGINT/SIGTERM/SIGHUP) arrived.
  bool shouldClose() const;

  bool close_ui_ = false;

  // Set once in start(), before wireLaunchpad() runs - shared here (rather
  // than duplicated per backend) since any UI frontend, GUI included, reads
  // it just as much as TerminalUI does today (device-state toggles, per-
  // device command resolution, handleLaunchpadChannelPressureEvent() above).
  LaunchpadManager * launchpad_manager_ = nullptr;

  // Set once in start(). Only MIDI touches it from the UI thread
  // (selectMidiInput()); every other audio change goes through the audio
  // thread's own event queue.
  AudioAPI * audio_ = nullptr;

 private:
  void initializeCommands();

  StatusLogger logger_;
  View view_ = View::ARRANGEMENT;
protected:
  View initial_view_ = View::LIVE;
private:
  bool outline_visible_ = true;
};

#endif
