#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"
#include "../util/Logger.h"

#include <string>

class AudioAPI;
class LaunchpadIO;
class LaunchpadManager;
class LaunchpadChannelPressureEvent;
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

  // Launchpad hardware input isn't tied to any one visual frontend, so its
  // event handling lives here rather than in a concrete backend - this one
  // is a pure passthrough to LaunchpadManager, with no widget dependency of
  // its own. handleLaunchpadPadEvent/handleLaunchpadButtonEvent stay
  // backend-defined for now (they reach into a concrete backend's own
  // current-note-entry-surface/command-dispatch state).
  void handleLaunchpadChannelPressureEvent(LaunchpadChannelPressureEvent & ev) override;

  // Which layout the active song is shown in - UI state, independent of
  // which buffer is active. Arrangement: the arrangement overview plus the
  // pattern editor. Session: the clip grid (with the optional outline
  // panel beside it) plus the pattern editor.
  enum class View { ARRANGEMENT, SESSION };
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

  Logger & getLogger() { return logger_; }

  bool close_ui_ = false;

  // Set once in start(), before wireLaunchpad() runs - shared here (rather
  // than duplicated per backend) since any UI frontend, GUI included, reads
  // it just as much as TerminalUI does today (device-state toggles, per-
  // device command resolution, handleLaunchpadChannelPressureEvent() above).
  LaunchpadManager * launchpad_manager_ = nullptr;

private:
  void initializeCommands();

  StatusLogger logger_;
  View view_ = View::ARRANGEMENT;
protected:
  View initial_view_ = View::SESSION;
private:
  bool outline_visible_ = true;
};

#endif
