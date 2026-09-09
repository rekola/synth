#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"
#include "../util/Logger.h"

#include <string>

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

// Toolkit-agnostic top-level application UI - owns nothing widget-shaped
// itself (a concrete subclass, e.g. TerminalUI, owns its own widget set and
// screen layout), just the app-level lifecycle every backend needs:
// spawning the audio/visualization threads, running until told to quit,
// and reporting status. A future non-terminal UI would subclass this
// directly rather than TerminalUI.
class UI : public UIElement {
 public:
  explicit UI() : logger_(this) { }

  virtual void refresh() = 0;
  virtual void render() = 0;

  // Spawns the audio/visualization threads, hands off to startUI() (the
  // concrete backend's own main loop) until it returns, then signals both
  // threads to stop and joins them.
  void start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager);

  virtual void setStatus(std::string s) = 0;

protected:
  virtual void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) = 0;

  // Hook for a concrete UI to wire up whatever per-widget Launchpad
  // callbacks it needs (session/track-move, drum-edit request, ...) -
  // default no-op, since a UI backend with no Launchpad-aware widgets of
  // its own needs none of them.
  virtual void wireLaunchpad(LaunchpadManager & launchpad_manager) { }

  Logger & getLogger() { return logger_; }

  bool close_ui_ = false;

private:
  StatusLogger logger_;
};

#endif
