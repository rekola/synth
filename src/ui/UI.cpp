#include "UI.h"

#include "UIMenu.h"
#include "Chart.h"
#include "HeatmapChart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "PatternEditor.h"
#include "PatternMatrix.h"
#include "HierarchyView.h"
#include "SpinBox.h"
#include "../model/Color.h"
#include "../audio/AudioAPI.h"
#include "../playback/Player.h"

#include "../playback/PlaybackEvent.h"
#include "../playback/LogEvent.h"
#include "../playback/RecordEvent.h"
#include "../playback/PlaybackControlEvent.h"
#include "../playback/AudioBlockEvent.h"
#include "../playback/VisualizationResultEvent.h"
#include "../playback/VisualizationThread.h"
#include "../Controller.h"
#include "KeyChord.h"
#include "../launchpad/LaunchpadButtonEvent.h"
#include "../launchpad/LaunchpadPadEvent.h"
#include "../launchpad/LaunchpadChannelPressureEvent.h"
#include "../launchpad/LaunchpadProtocol.h"
#include "../launchpad/LaunchpadManager.h"
#include "../model/DrumMachineTrack.h"
#include "../bus/BusEffectRegistry.h"
#include "../util/constants.h"

#include <fmt/core.h>
#include <thread>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace std;
using namespace fmt;

void
UI::requestOverviewFocus() {
  // Lands on the overview's own last (rightmost) column, not wherever its
  // cursor happened to be left last time - both entry points (PatternEditor's
  // leftmost track, Launchpad's prev-track already at track 0) arrive
  // "from the right". Not symmetric with exitOverview() (always the first
  // track, regardless of which column was current here) - deliberately:
  // exiting always returns to the same, predictable starting point.
  auto num_tracks = static_cast<int>(pattern_matrix_->getVisibleTrackIds(getController().getSong()).size());
  pattern_matrix_->setCursorTrackIndex(max(0, num_tracks - 1));
  active_element_ = pattern_matrix_;
}

void
UI::exitOverview() {
  pattern_editor_->setCursorTrack(0);
  active_element_ = pattern_editor_;
}

void
UI::commitOverviewCell(int track_id, int scene_idx) {
  // The whole commit refuses while playing - matches PatternEditor's own
  // move-row-up/move-row-down guard (row navigation only ever runs while
  // stopped), and avoids a commit that silently only did half of what it
  // normally does (move the track but not the playhead) while playing.
  if (getController().getPlaybackInfo().isPlaying()) return;

  auto & song = getController().getSong();
  // NOTE: if PatternEditor's own mark/selection happens to still be active
  // (selection_active_, unrelated to anything PatternMatrix/Launchpad
  // does), setEditPosition() below clamps to the pattern that selection is
  // in rather than actually jumping to scene_idx - a real, narrow edge
  // case, not handled here.
  getController().setEditPosition(scene_idx * song.getPatternLength());

  auto track_ids = song.getRootTrackIds();
  auto it = find(track_ids.begin(), track_ids.end(), track_id);
  if (it != track_ids.end()) {
    pattern_editor_->setCursorTrack(static_cast<int>(it - track_ids.begin()));
  }
  active_element_ = pattern_editor_;
}

void
UI::initialize() {
  // chart and volume are missing
  pattern_editor_ = make_shared<PatternEditor>(getPlane());
  pattern_matrix_ = make_shared<PatternMatrix>(getPlane());
  // Enter commits the cell under PatternMatrix's own (local, passive)
  // cursor to shared state - see PatternMatrix.h's own comment on why this
  // is a callback rather than PatternMatrix reaching for PatternEditor/
  // active_element_ itself (it doesn't know either exists).
  // launchpad_manager_ isn't set yet at this point (UI::start() assigns
  // it later, after main.cpp's own ui.initialize()/ui.start() call order -
  // see that method for the equivalent Launchpad wiring), so that half of
  // commitOverviewCell()'s callers is wired there instead.
  pattern_matrix_->setCommitCallback([this](int track_id, int scene_idx) { commitOverviewCell(track_id, scene_idx); });
  // Plain Left with nowhere further left to go - see PatternEditor's own
  // setOverviewRequestCallback() comment; Launchpad's prev-track hits the
  // same edge, wired in UI::start() below for the same launchpad_manager_-
  // isn't-set-yet reason commitOverviewCell()'s own split wiring is.
  pattern_editor_->setOverviewRequestCallback([this]() { requestOverviewFocus(); });
  // The reverse edge: Right past the last (rightmost) visible column -
  // lands on PatternEditor's own first track, same landing spot Launchpad's
  // own overview-exit already uses (see UI::start()'s equivalent wiring).
  pattern_matrix_->setExitRightCallback([this]() { exitOverview(); });
  info_line_ = make_shared<InfoLine>(getPlane());
  status_line_ = make_shared<StatusLine>(getPlane());
  // See StatusLine::showPrompt()'s own comment: opening any StatusLine
  // reader (M-x included) must take focus away from PatternEditor's own
  // annotation/track-name editor rather than opening on top of it.
  status_line_->setBeforeShowPromptCallback([this]() { pattern_editor_->cancelReaderEdit(); });
  // Colors match InfoLine's own hardcoded gray-on-dark (InfoLine.h's
  // constructor) - this widget sits inline in that same bar (see
  // layout()), and must blend into it rather than showing up as a
  // mismatched patch.
  octave_control_ = make_shared<SpinBox>(getPlane(), "Octave:", constants::MIN_OCTAVE, constants::MAX_OCTAVE,
    [this] { return getController().getGlobalOctave(); },
    [this](int v) { getController().setGlobalOctave(v); },
    Color(120, 120, 120), Color(30, 30, 30));

#if 0
  windows_.push_back(make_shared<HierarchyView>(getPlane()));
#endif

  // Session/overview first, not pattern editing - matches the Launchpad's
  // own Session view as the more approachable starting point for a fresh
  // buffer (a bird's-eye view of tracks/scenes) rather than dropping
  // straight into note-by-note editing.
  active_element_ = pattern_matrix_;

  commands_.define("save-buffers-kill-terminal", [this]() {
    if (getController().hasAnyUnsavedChanges()) {
      status_line_->showPrompt("Unsaved changes in one or more buffers - discard and quit? (y/n) ", [this](const std::string & answer) {
	if (answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes") close_ui_ = true;
      });
    } else {
      close_ui_ = true;
    }
  });
  // Emacs's own find-file prompt/name, prefilled with the active buffer's
  // own directory (its find-file's default-directory equivalent - there's
  // no separate notion of "current directory" here beyond that) so typing
  // just a bare filename opens a sibling of whatever's already open,
  // matching find-file's own "already positioned in the right directory"
  // convenience. Tab/Enter complete against the real filesystem
  // (StatusLine::completeFilePath()), same as Emacs's own find-file. No
  // discard-confirmation (unlike this used to need before buffers existed
  // - see songs_'s own comment on Controller.h): opening a file that's
  // already an open buffer just switches to it (Controller::openSong()),
  // and opening a new one adds a buffer rather than replacing the current
  // one.
  commands_.define("open-song", [this]() {
    auto dir = std::filesystem::path(getController().getActiveBufferName()).parent_path().string();
    if (!dir.empty()) dir += "/";
    status_line_->showFilePrompt("Find file: ", [this](const std::string & filename) {
      if (filename.empty()) return;
      if (getController().openSong(filename)) {
	setStatus("Opened " + filename);
      } else {
	setStatus("Could not open " + filename);
      }
    }, dir);
  });
  commands_.define("next-buffer", [this]() {
    getController().cycleBuffer(true);
  });
  commands_.define("previous-buffer", [this]() {
    getController().cycleBuffer(false);
  });
  // Tab/Enter complete against the open buffer names (StatusLine::
  // completeAgainstSet(), the same machinery M-x's own command-name
  // completion uses); switchToBuffer() creates a fresh blank buffer for a
  // name that isn't already open, same as Emacs's own switch-to-buffer, so
  // this doubles as "New" too (there's no separate new-song command any
  // more) - typing an unrecognized name and hitting Enter still works even
  // though it'll never complete to anything. Prompt text and the empty-
  // answer-means-default behavior both match Emacs's own "Switch to buffer
  // (default ...): " convention (read-buffer-to-switch) - see
  // Controller::getDefaultSwitchTarget()'s own comment for what "default"
  // means without real MRU buffer tracking.
  commands_.define("select-named-buffer", [this]() {
    auto default_name = getController().getDefaultSwitchTarget();
    auto prompt = default_name.empty() ? "Switch to buffer: " : ("Switch to buffer (default " + default_name + "): ");
    status_line_->showPromptWithCompletion(prompt, [this, default_name](const std::string & name) {
      auto target = name.empty() ? default_name : name;
      if (target.empty()) return; // nothing typed and no default to fall back to
      getController().switchToBuffer(target);
    }, [this](const std::string & prefix) {
      std::set<std::string> result;
      for (auto & name : getController().getBufferNames()) {
	if (name.compare(0, prefix.size(), prefix) == 0) result.insert(name);
      }
      return result;
    });
  });
  commands_.define("kill-buffer", [this]() {
    auto doKill = [this]() {
      auto name = getController().getActiveBufferName();
      if (getController().killActiveBuffer()) {
	setStatus("Killed " + name);
      } else {
	setStatus("Can't kill the only open buffer");
      }
    };
    if (getController().hasUnsavedChanges()) {
      status_line_->showPrompt("Buffer modified - kill anyway? (y/n) ", [doKill](const std::string & answer) {
	if (answer == "y" || answer == "Y" || answer == "yes" || answer == "Yes") doKill();
      });
    } else {
      doKill();
    }
  });
  commands_.define("toggle-playing", [this]() {
    bool playing = getController().togglePlaying();
    setStatus(playing ? "Playing" : "Stopped");
  });
  // Global, not PatternEditor-owned (unlike before - see PatternEditor.cpp's
  // own history) - both computer-keyboard note entry and every connected
  // Launchpad's own octave read Controller::getGlobalOctave(), so these
  // two keys should work regardless of which widget currently has focus,
  // matching "toggle-playing"/Space just above. octave_control_'s own
  // [-]/[+] buttons call the exact same Controller methods.
  commands_.define("octave-up", [this]() { getController().octaveUp(); });
  commands_.define("octave-down", [this]() { getController().octaveDown(); });
  commands_.define("save-song", [this]() {
    getController().sendCommand("save-song");
    setStatus("Saved " + getController().getActiveBufferName());
  });
  commands_.define("save-song-as", [this]() {
    status_line_->showPrompt("Save as: ", [this](const std::string & filename) {
      if (filename.empty()) return;
      getController().saveSongAs(filename);
      setStatus("Saved " + filename);
    }, getController().getActiveBufferName());
  });

  // "Set Bus Effect A/B..." (Song menu) - picks among the fixed 5-entry
  // bus/BusEffectRegistry.h registry (none/reverb/delay/granular/haze) via
  // the same showPromptWithCompletion() a buffer name/M-x command already
  // completes against; findBusEffectDescriptor() rejects anything outside
  // the registry, reporting it rather than silently no-oping. Prefilled
  // with the slot's current effect name, so Enter alone reaffirms it
  // unchanged. Controller::setBusEffectKind() both updates the Song model
  // (so it's still this slot's occupant on the next save/fresh load) and
  // pushes the matching PlaybackControlEvent so an already-playing buffer's
  // live SongState picks up the new effect immediately too - same
  // "model plus event" shape as e.g. toggleTrackMuted()/setTrackSendA().
  auto setBusEffect = [this](int slot, const char * slot_label) {
    auto current = findBusEffectDescriptor(getController().getSong().getBusSlotKind(slot)).xmlName;
    status_line_->showPromptWithCompletion(std::string("Bus Effect ") + slot_label + ": ",
      [this, slot, slot_label](const std::string & typed) {
	if (typed.empty()) return;
	auto * descriptor = findBusEffectDescriptor(typed);
	if (!descriptor) { setStatus("Unknown bus effect: " + typed); return; }
	getController().setBusEffectKind(slot, descriptor->kind);
	setStatus(std::string("Bus Effect ") + slot_label + " set to " + descriptor->xmlName);
      },
      [](const std::string & prefix) {
	std::set<std::string> result;
	for (auto & entry : busEffectRegistry()) {
	  std::string name = entry.xmlName;
	  if (name.compare(0, prefix.size(), prefix) == 0) result.insert(name);
	}
	return result;
      }, current);
  };
  commands_.define("set-bus-effect-a", [setBusEffect]() { setBusEffect(0, "A"); });
  commands_.define("set-bus-effect-b", [setBusEffect]() { setBusEffect(1, "B"); });

  // C-x C-x (exchange-point-and-mark) is a PatternEditor-owned command (the
  // mark/point state it swaps lives there, alongside set-mark/kill-region -
  // see PatternEditor.cpp's own definition) - forwarded through
  // executeCommand() the same way UI::executeCommand()'s own active-element
  // fallback would, since the C-x prefix itself is only ever recognized at
  // this level (dispatchCommand() below checks *this* registry, not
  // PatternEditor's - unlike the M-x path, which does go through that
  // fallback chain).
  commands_.define("exchange-point-and-mark", [this]() { pattern_editor_->executeCommand("exchange-point-and-mark"); });

  // Quit/save/open/save-as use Emacs's own C-x C-c/C-x C-s/C-x C-f/C-x C-w
  // bindings and command names (save-buffers-kill-terminal/save-buffer/
  // find-file/write-file, the first three shortened to save-song/
  // open-song/save-song-as here to match this codebase's own pre-existing
  // M-x command naming - see Controller::sendCommand()) rather than
  // one-off single-key shortcuts or made-up names - see
  // Keymap::bindPrefixed()/UIElement::dispatchCommand() for the two-key
  // prefix-sequence machinery this needs. No Ctrl-N/"New" binding any
  // more - see select-named-buffer's own comment above for why there's no
  // separate "New" command left to bind.
  auto ctrl_x = KeyChord::pack('x', true, false, false, false);
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('c', true, false, false, false), "save-buffers-kill-terminal");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('s', true, false, false, false), "save-song");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('f', true, false, false, false), "open-song");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('w', true, false, false, false), "save-song-as");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('x', true, false, false, false), "exchange-point-and-mark");
  // The buffer commands' own real Emacs bindings, unlike every C-x C-<letter>
  // above, hold Ctrl for the C-x prefix only, not the second key: kill-buffer
  // is C-x k (plain k), next-buffer/previous-buffer are C-x <right>/C-x
  // <left>, and select-named-buffer (switch-to-buffer) is C-x b.
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('k', false, false, false, false), "kill-buffer");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack(NCKEY_RIGHT, false, false, false, false), "next-buffer");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack(NCKEY_LEFT, false, false, false, false), "previous-buffer");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('b', false, false, false, false), "select-named-buffer");
  keymap_.bind(KeyChord::pack(' ', false, false, false, false), "toggle-playing");
  keymap_.bind(KeyChord::pack('[', false, false, false, false), "octave-down");
  keymap_.bind(KeyChord::pack(']', false, false, false, false), "octave-up");

  assertCommandBindingsValid();

  // Lets StatusLine's M-x path (Controller::sendCommand, which only reaches
  // Controller/Song-level state) also invoke commands owned by UI or by
  // whichever widget is currently active, without Controller depending on
  // any UI type - see Controller.h's command_fallback_.
  getController().setCommandFallback([this](std::string_view name) { return executeCommand(name); });
  // Read-only sibling of the fallback above, wired the same way and for
  // the same reason - StatusLine's M-x autocomplete needs to reach
  // per-widget command names too, not just Controller's own.
  getController().setCommandCompleter([this](std::string_view prefix) { return commandCompletions(prefix); });
  // Keeps the Buffers menu's item list/active-buffer marker in sync with
  // every buffer change, not just the ones the commands_ lambdas above
  // trigger directly - a Buffers-menu click straight on a buffer's own
  // row resolves entirely inside Controller's own commands_
  // ("switch-to-buffer:<name>", Controller::refreshBufferCommands()) and
  // never otherwise reaches UI, so relying on each buffer command here to
  // separately call menu_->refreshBuffers() itself missed that path
  // entirely - one listener covers every path uniformly instead.
  getController().setBufferChangeListener([this]() {
    auto names = getController().getBufferNames();
    vector<string> display_names;
    display_names.reserve(names.size());
    for (auto & name : names) display_names.push_back(getController().getBufferDisplayName(name));
    menu_->refreshBuffers(names, display_names, getController().getActiveBufferName());

    // Cursor/scroll/selection/live-note/annotation-editing state - see
    // PatternEditor::handleBufferChanged()'s own comment.
    pattern_editor_->handleBufferChanged();
  });
}

bool
UI::executeCommand(std::string_view name) {
  // A command reached here other than through M-x's own submission (a menu
  // click, Launchpad-by-name dispatch) always takes focus away from
  // whatever M-x was reading, same as StatusLine::cancelReader()'s own
  // Ctrl-g - mirrors real Emacs, where clicking a menu item while typing
  // in the minibuffer just aborts the minibuffer read and runs the
  // command, rather than the command running while the minibuffer keeps
  // consuming keystrokes underneath it. A no-op for M-x's own path: by the
  // time a typed command reaches here, submitReader() already closed the
  // reader before ever calling sendCommand().
  if (status_line_->isReaderActive()) status_line_->cancelReader();
  if (auto el = active_element_.lock()) {
    if (el->executeCommand(name)) return true;
  }
  // The pattern editor is the default/main workspace (same precedent
  // UI::offerInput()'s BUTTON1 handling already establishes: a click that
  // lands nowhere else falls back to it) - a command reached through here
  // (M-x, a menu item, a Launchpad-by-name dispatch) rather than a direct
  // keystroke has no click position to fall back from, so it gets the same
  // fallback here instead: whatever's actually focused gets first refusal,
  // but a command that widget doesn't own still reaches the pattern editor
  // rather than failing just because some other, unrelated widget happens
  // to be active. Skipped when the pattern editor already had first
  // refusal above (active_element_ == pattern_editor_) - trying it twice
  // would be harmless but pointless.
  if (auto el = active_element_.lock(); el != pattern_editor_) {
    if (pattern_editor_->executeCommand(name)) return true;
  }
  return UIElement::executeCommand(name);
}

std::set<std::string>
UI::commandCompletions(std::string_view prefix) const {
  // A set, not a list - more than one source below can legitimately define
  // the same command name (e.g. a widget-local override), and "every known
  // command name" is a set to begin with; ordered so a later phase can show
  // the candidates sorted without a separate sort step.
  set<std::string> result;
  auto append = [&](const vector<std::string> & names) { result.insert(names.begin(), names.end()); };
  if (auto el = active_element_.lock()) {
    append(el->commandNames(prefix));
  }
  if (auto el = active_element_.lock(); el != pattern_editor_) {
    append(pattern_editor_->commandNames(prefix));
  }
  append(UIElement::commandNames(prefix));
  return result;
}

void
UI::layout() { 
  auto [ rows, cols ] = getDim();
  setStatus("Layout (rows = " + to_string(rows) + ", cols = " + to_string(cols) + ")");

  constexpr int kHeatmapWidth = 31; // 20 * 1.5, rounded up to the nearest odd width
  constexpr int kScopeRow = 1, kScopeHeight = 5;

  // pattern_matrix_ claims the scope row's own leftmost columns first (the
  // literal top-left corner) - sized off the song's own track count but
  // capped, since its own internal scrolling handles anything past that
  // rather than this widget ever needing to be as wide as the track list
  // is long. chart_ gives up exactly that much width (plus one divider
  // column) to make room; heatmap_/volume_meter_ are untouched - a
  // terminal too narrow for all four just clamps chart_ down toward 1
  // column rather than a fuller "drop the least-used scope first"
  // rebalance, deferred for now.
  auto num_tracks = static_cast<int>(getController().getSong().getPlayableTrackIds().size());
  // PatternMatrix spends 2 columns per track (its own cell plus a blank
  // separator - see PatternMatrix.cpp's own kColWidth), so its width needs
  // doubling here to actually fit the same track count this clamp implies.
  int matrix_width = std::clamp(num_tracks, 4, 24) * 2;
  int matrix_divider_x = matrix_width;
  pattern_matrix_->resize(kScopeHeight, matrix_width).move(kScopeRow, 0);

  int chart_width = std::max(1, cols - (matrix_width + 1) - 9 - kHeatmapWidth - 2); // -2 for the single-column dividers on either side of the heatmap
  int chart_x = matrix_width + 1;
  int divider1_x = chart_x + chart_width, divider2_x = divider1_x + 1 + kHeatmapWidth;
  chart_->resize(kScopeHeight, chart_width).move(kScopeRow, chart_x);
  heatmap_->resize(kScopeHeight, kHeatmapWidth).move(kScopeRow, divider1_x + 1);
  volume_meter_->resize(kScopeHeight, 9).move(kScopeRow, divider2_x + 1);

  // Single-column dividers between the four scopes - drawn once here
  // rather than per-frame, since these columns fall outside every scope's
  // own resized rectangle, so nothing else ever repaints over them.
  setFgColor(styles_.window_border_color);
  setBgColor(styles_.window_bg_color);
  for (int row = 0; row < kScopeHeight; row++) {
    putstr(kScopeRow + row, matrix_divider_x, "│");
    putstr(kScopeRow + row, divider1_x, "│");
    putstr(kScopeRow + row, divider2_x, "│");
  }
  pattern_editor_->resize(rows - 8, cols).move(6, 0);
  info_line_->resize(1, cols).move(rows - 2, 0);
  // Inline in the info bar, right-aligned - a separate plane, created
  // after info_line_ (UI::initialize()) so it z-orders above whatever
  // info_line_'s own text/padding puts under it (see TerminalUI.cpp's
  // menu_->raiseToTop() comment for this codebase's "later-created sits
  // above" plane-stacking rule). If the info bar gets too full for this
  // later, this is the one line to change to move it into a dedicated
  // control bar row instead.
  auto octave_width = octave_control_->preferredWidth();
  octave_control_->resize(1, octave_width).move(rows - 2, std::max(0, cols - octave_width));
  status_line_->resize(1, cols - 1).move(rows - 1, 0);

  for (auto & window : windows_) {
    window->resize(rows - 7, cols).move(5, 0);
  }
}

bool
UI::renderComponents(bool refresh) {
  bool render = false;
  auto active = active_element_.lock();
  render |= pattern_editor_->render(styles_, refresh, active == pattern_editor_);
  render |= pattern_matrix_->render(styles_, refresh, active == pattern_matrix_);
#if 0
  for (auto & window : windows_) {
    render |= window->render(styles_, refresh);
  }
#endif
  render |= info_line_->render(styles_, refresh);
  render |= octave_control_->render(styles_, refresh);

  if (launchpad_manager_) {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    // Populated every call regardless of which UI element actually has
    // focus - a Launchpad's own Session view (CC95/96, per-device) is
    // independent of that now, so this always needs to be ready with
    // wherever a press would actually land (see LaunchpadManager::
    // SessionWindow's own comment).
    LaunchpadManager::SessionWindow session;
    session.track_ids = pattern_matrix_->getVisibleTrackIds(song);
    session.cursor_scene_idx = pattern_matrix_->getCursorScene();
    launchpad_manager_->refresh(song, track_ids, getController().getPlaybackInfo(),
      track_ids.empty() ? -1 : pattern_editor_->getCursorTrackIndex(), getController(), session);
  }

  return render;
}

bool
UI::tryActivate(int y, int x, std::shared_ptr<UIElement> element) {
  auto [pos_y, pos_x] = element->getPosition();
  auto [rows, cols] = element->getDim();

  if (y >= pos_y && y < pos_y + rows && x >= pos_x && x < pos_x + cols) {
    active_element_ = element;
    return true;
  } else {
    return false;
  }
}

bool
UI::offerInput(const InputEvent & input) {
  bool handled = false;

  // Neither reader (StatusLine's M-x minibuffer, PatternEditor's own
  // annotation editor - see PatternEditor::isReaderActive()'s own comment)
  // may let a global keybinding (Space/toggle-playing, C-x C-c/quit, ...)
  // steal a keystroke meant for it.
  if (!status_line_->isReaderActive() && !pattern_editor_->isReaderActive() && !octave_control_->isEditing() && dispatchCommand(input)) return true;

  if (input.getId() == NCKEY_RESIZE) {
    // notcurses_refresh() is what makes notcurses acknowledge the terminal's
    // new dimensions (they're otherwise stale until this is called); it
    // must run before anything queries plane sizes or lays out against them.
    refresh();
    getPlane().refresh();
    layout();
    renderComponents(true);
  } else if (input.hasCtrl() && input.getId() == 'l') {
    refresh();
  } else if (input.getId() == NCKEY_BUTTON1) {
    auto previous_active_element = active_element_.lock();
    active_element_.reset();

    // status_line_ deliberately isn't a tryActivate() candidate: its own
    // input handling (the M-x trigger, and its reader while one's open) is
    // already reached unconditionally a few lines below, regardless of
    // active_element_ - becoming the active element bought it nothing, and
    // cost everything else, since its own offerInput() returns false for
    // any keystroke that isn't M-x/reader-related, so the active_element_
    // fallback stopped reaching pattern_editor_ at all (no widget owns
    // plain arrow keys/note entry the way pattern_editor_ does) the moment
    // a click landed anywhere on the status line's own row - which spans
    // the entire bottom row, so this was very easy to trigger by accident.
    bool activated = tryActivate(input.getY(), input.getX(), pattern_editor_);
    activated = tryActivate(input.getY(), input.getX(), pattern_matrix_) || activated;

    for (auto & window : windows_) {
      activated = tryActivate(input.getY(), input.getX(), window) || activated;
    }

    activated = tryActivate(input.getY(), input.getX(), octave_control_) || activated;

    // Fall back to the pattern editor - the default/main workspace - if the
    // click landed somewhere no widget claims (e.g. the FFT/heatmap/loudness
    // scope strip, or the dividers between them). Without this, active_element_
    // was left permanently empty (a real, confirmed bug): every subsequent
    // keyboard command routed through it (UI::offerInput()'s active_element_
    // fallback below, and Launchpad button commands via UI::executeCommand())
    // silently no-op'd - including plain Up/Down arrow - until the user
    // happened to click directly back on the pattern editor.
    if (!activated) active_element_ = pattern_editor_;

    // A click that moves focus away from the octave stepper while it's
    // mid-edit discards the half-typed value rather than leaving it stuck
    // showing a stale "selected" field forever - see SpinBox::
    // cancelEditing()'s own comment. Not needed when the click lands back
    // on octave_control_ itself (its own offerInput(), called below via
    // active_element_, handles that click directly).
    if (previous_active_element == octave_control_ && active_element_.lock() != octave_control_) {
      octave_control_->cancelEditing();
    }
  }

  if (!handled) {
    handled |= menu_->offerInput(input);
    if (handled) {
      // ncmenu tracks an item's display text, not any notion of a command -
      // TerminalMenu::offerInput() maps activation (a click on an item, or
      // Enter while one is highlighted) to a command name itself; this is
      // where it actually gets run. Goes through Controller::sendCommand()
      // - not executeCommand() directly - for the same reason M-x
      // (StatusLine::showMx()) does: sendCommand() tries its own
      // Controller-level chain (toggle-mixer-type, ...) first and only
      // then falls back to executeCommand() itself (Controller::
      // setCommandFallback(), UI::initialize()); calling executeCommand()
      // directly would skip that chain entirely, silently no-oping any
      // menu item mapped to a Controller-only command. Failure reported
      // the same way M-x reports it, for the same reason (a mistyped/
      // stale command name should never fail silently).
      if (auto cmd = menu_->takeActivatedCommand(); !cmd.empty()) {
	if (!getController().sendCommand(cmd)) setStatus("Invalid command");
      }
    }
  }
  if (!handled) {
    handled |= status_line_->offerInput(input);
  }

  if (!handled) {
    if (auto el = active_element_.lock()) {
      handled |= el->offerInput(input);
    }
  }
    
  return handled;
}

void
UI::setStatus(std::string s) {
  if (status_line_) {
    status_line_->setMessage(std::move(s));
    render();
  }
}

void
UI::handlePlaybackEvent(PlaybackEvent & ev) {
  // Reconciled, not a plain setPlaybackInfo() - see Controller::
  // receivePlaybackSnapshot()'s own comment: this snapshot's own
  // edit-position fields can be stale relative to a more recent local
  // moveEditPosition()/setEditPosition() prediction.
  getController().receivePlaybackSnapshot(ev.getBufferName(), ev.getInfo());

  // Every remaining reaction below (auto-record row-sweep, redraw) is only
  // meaningful for whichever buffer is currently being looked at/edited -
  // Player now pushes one snapshot per live buffer every block (see the
  // per-buffer editing/playback-state plan's Part B), and a snapshot for
  // some other buffer (e.g. one still playing in the background while a
  // different one is active) already landed in its own map slot above,
  // with nothing on screen that depends on it right now.
  if (ev.getBufferName() != getController().getActiveBufferName()) return;

  // Must run right after receivePlaybackSnapshot() above, before any other event
  // (a pad press, a keystroke) that might read the just-updated row and
  // write a note there - a no-op outside an active realtime-recording
  // session, and cheap even then (only rows the playhead actually just
  // passed get touched). This ordering is what makes the whole-row-clear
  // feature race-free: by the time any note write can possibly see the
  // new row, the clear for it (if any) has already happened, all within
  // this same synchronous call - see LaunchpadManager::onRowAdvanced()'s
  // own comment for the full reasoning.
  if (launchpad_manager_) launchpad_manager_->onRowAdvanced(getController());
  if (pattern_editor_) pattern_editor_->onRowAdvanced(getController());

  ev.redraw();
}

void
UI::handleVisualizationResultEvent(VisualizationResultEvent & ev) {
  // If a newer event is already queued behind this one, this one's visual
  // result is about to be immediately overwritten - skip the
  // comparatively expensive chart/meter update work for it. Doesn't
  // change what eventually gets rendered (the last event in a batch
  // always wins anyway, via plain overwrite); it only avoids redoing that
  // work once per superseded event during a catch-up burst, so the app
  // catches up faster instead of falling further behind.
  bool superseded = getController().getUIEventQueue().hasEvents();
  if (!superseded) {
    // Raw, pre-mixdown per-channel levels (ambisonic bus, then always
    // AuxA/AuxB last - see VisualizationThread.cpp) rather than the final
    // decoded L/R output. Always fills the full fixed-size domain
    // (kMaxMeterChannels - the order-3-ambisonic+2-aux max), padding with
    // silence past the current config's real channel count - matching
    // displayFFT()'s own always-fill-the-whole-domain contract below
    // (every index, every call). Feeding a varying, sometimes-shorter
    // range confused the underlying plot's own domain/alignment (bars
    // for a smaller config visibly started mid-width instead of at
    // column 0, out of step with the legend) - a fixed domain avoids
    // that.
    auto & levels = ev.getChannelLoudness();
    volume_meter_->setFooterLabel(ev.getMeterLabel());
    for (size_t i = 0; i < kMaxMeterChannels; i++) {
      volume_meter_->setSample(static_cast<int>(i), i < levels.size() ? levels[i] : 0.0);
    }
    volume_meter_->commit();

    if (!ev.getFFT().empty()) {
      chart_->displayFFT(ev.getFFT());
    }

    if (ev.hasDiracGrid()) {
      // plans/dirac-heatmap-scope.md SS6: displayed[cell] = the grid's own
      // directional energy plus the per-band diffuse haze, spread
      // uniformly across every cell (one shared scalar summed from all 8
      // bands, not a separate per-band-per-cell splat).
      auto & grid = ev.getDiracGrid();
      auto & diffuse_energy = ev.getDiracDiffuseEnergy();
      float diffuse_sum = 0.0f;
      for (auto e : diffuse_energy) diffuse_sum += e;
      float local_diffuse = diffuse_sum / static_cast<float>(DiracAnalyzer::kGridSize);

      std::array<float, DiracAnalyzer::kGridSize> displayed;
      float frame_max = 0.0f;
      for (size_t i = 0; i < DiracAnalyzer::kGridSize; i++) {
        displayed[i] = grid[i] + local_diffuse;
        if (displayed[i] > frame_max) frame_max = displayed[i];
      }

      // Auto-scaling brightness reference, tracked across events rather
      // than derived fresh each time: jumps up immediately on a new peak
      // (so a loud transient doesn't clip the display), decays slowly
      // otherwise (~2s time constant at this event's ~28.7Hz delivery
      // rate - plans/dirac-heatmap-scope.md SS1) so a quiet passage
      // doesn't suddenly wash the whole grid out to full brightness the
      // instant a loud part ends.
      if (frame_max > dirac_running_max_) dirac_running_max_ = frame_max;
      else dirac_running_max_ += (frame_max - dirac_running_max_) * 0.0173f;

      // log1p applied to the *ratio* to dirac_running_max_ (not to the raw
      // absolute displayed[i]/dirac_running_max_ values themselves) - taking
      // log1p of an un-normalized absolute magnitude made the compression
      // severity (and so the perceived fade time of a decaying cell) scale
      // with how loud the audio was: log1pf(displayed)/log1pf(max) only
      // approaches 0 once displayed drops below O(1) in absolute terms, so a
      // louder passage (larger running_max, in arbitrary energy units) left
      // a decayed cell sitting at a substantial fraction of full brightness
      // long after its energy had genuinely fallen away - e.g. at 1% of
      // peak it could still read ~35% bright. Normalizing to a ratio first
      // ties the curve to *relative* loudness instead, so "decayed to 1% of
      // peak" always maps to roughly the same low brightness regardless of
      // the absolute scale - kRatioCompression trades off shadow detail
      // against how quickly a decaying cell now visibly reads as "gone".
      constexpr float kRatioCompression = 16.0f;
      float log_max = log1pf(kRatioCompression);
      std::vector<float> brightness(DiracAnalyzer::kGridSize), saturation(DiracAnalyzer::kGridSize);
      for (size_t i = 0; i < DiracAnalyzer::kGridSize; i++) {
        float ratio = dirac_running_max_ > 0.0f ? displayed[i] / dirac_running_max_ : 0.0f;
        brightness[i] = log1pf(kRatioCompression * ratio) / log_max;
        if (brightness[i] > 1.0f) brightness[i] = 1.0f;
        saturation[i] = displayed[i] > 1e-12f ? grid[i] / displayed[i] : 0.0f;
      }
      heatmap_->setGrid(brightness, saturation);
      heatmap_->commit();
    }
  }

  ev.redraw();
}

void
UI::handleRecordEvent(RecordEvent & ev) {
  if (getController().isRecording()) {
    setStatus(format("recorded {} frames", ev.getData().size()));
    getController().addToSample(ev.getData());
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & scene = song.getScene(info.getPatternIndex());
    scene.setNote(info.getRowIndex(), getController().getRecordingTrackId(), 0, Note(1));
  }
}

void
UI::handleLogEvent(LogEvent & ev) {
  setStatus(ev.getText());
}

void
UI::handleMidiEvent(MidiEvent & ev) {
  pattern_editor_->handleMidiEvent(ev);
}

void
UI::handleLaunchpadPadEvent(LaunchpadPadEvent & ev) {
  // DRAW mode (a plain coloring toy - see LaunchpadManager::
  // pressDrawPad/releaseDrawPad) touches no Song/Track/Pattern data at
  // all, unlike every other pad-event use (note entry, Send A/B/Pan) -
  // handled entirely here, before PatternEditor (which owns actual
  // pattern editing) ever sees the event.
  if (launchpad_manager_ && launchpad_manager_->gridMode(ev.getDeviceIndex()) == LaunchpadManager::GridMode::DRAW) {
    if (ev.getKind() == LaunchpadPadEvent::PRESS) {
      launchpad_manager_->pressDrawPad(ev.getDeviceIndex(), ev.getX(), ev.getY(), ev.getVelocity());
    } else if (ev.getKind() == LaunchpadPadEvent::AFTERTOUCH) {
      launchpad_manager_->updateDrawIntensity(ev.getDeviceIndex(), ev.getX(), ev.getY(), ev.getVelocity());
    } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
      launchpad_manager_->releaseDrawPad(ev.getDeviceIndex(), ev.getX(), ev.getY());
    }
    return;
  }
  // GridMode::SESSION: unlike DRAW above, this one does need Controller -
  // an "assign" press writes into the Song directly, and either sub-mode
  // (audition/assign - see handleSessionPadEvent()'s own comment) needs
  // the playback event queue.
  if (launchpad_manager_ && launchpad_manager_->gridMode(ev.getDeviceIndex()) == LaunchpadManager::GridMode::SESSION) {
    launchpad_manager_->handleSessionPadEvent(ev, getController());
    return;
  }
  if (!launchpad_manager_) return;
  launchpad_manager_->handlePadEvent(ev, getController(),
    pattern_editor_->getCursorTrackIndex(), pattern_editor_->getEditStepSize());
}

void
UI::handleLaunchpadChannelPressureEvent(LaunchpadChannelPressureEvent & ev) {
  if (!launchpad_manager_) return;
  launchpad_manager_->handleChannelPressureEvent(ev, getController());
}

void
UI::handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) {
  if (!launchpad_manager_) return;

  auto device_id = ev.getDeviceIndex();

  // CC49 ("Stop Clip") and CC97 ("Custom") both need press and release,
  // not just press - CC49 for its own tap-vs-long-hold picker/Clear
  // gesture (LaunchpadManager::handleStopClipButton()'s own comment), CC97
  // for DRAW mode's tap-vs-long-hold toggle/blank-canvas gesture
  // (LaunchpadManager::handleDrawToggleButton()). Routed here before the
  // press-only filter below, which every other raw-CC button (and every
  // other release) still goes through unchanged.
  if (ev.getCCNumber() == 49) {
    auto track_ids = getController().getSong().getPlayableTrackIds();
    auto track_id = launchpad_manager_->resolveTrackId(device_id, track_ids, pattern_editor_->getCursorTrackIndex());
    auto track = getController().getSong().getMasterTrack().getChildByInternalId(track_id);
    bool is_drum_machine = track && track->getType() == TrackType::DRUM_MACHINE;
    auto * drum_track = is_drum_machine ? &static_cast<DrumMachineTrack &>(*track) : nullptr;
    launchpad_manager_->handleStopClipButton(device_id, ev.getKind() == LaunchpadButtonEvent::PRESS, drum_track, getController());
    return;
  }
  if (ev.getCCNumber() == 97) {
    launchpad_manager_->handleDrawToggleButton(device_id, ev.getKind() == LaunchpadButtonEvent::PRESS);
    return;
  }

  if (ev.getKind() != LaunchpadButtonEvent::PRESS) return;

  // Send A/B: a direct hardware-state toggle (this device's own transient
  // grid-display mode), never a command - intercepted here, by raw CC
  // number, before any command-name resolution happens at all. See
  // LaunchpadManager::handleRawButton's own comment.
  if (launchpad_manager_->handleRawButton(ev.getCCNumber(), device_id)) return;

  auto name = LaunchpadProtocol::commandForButton(ev.getCCNumber());
  if (!name) return;

  // Emacs prefix-argument style: resolve which track_id this specific
  // physical device currently targets and stash it as a one-shot
  // transient on Controller before dispatching - "toggle-mute" (and any
  // future command that cares) reads-and-clears it, falling back to the
  // shared cursor's own track otherwise (see PatternEditor's constructor,
  // Controller::consumePendingCommandTrack). Harmless to set
  // unconditionally, even for commands that never consume it (octave-up,
  // next-track, ...) - it's a one-shot value, overwritten or cleared by
  // the very next dispatch either way, so it can never leak into a later,
  // unrelated command.
  auto track_ids = getController().getSong().getPlayableTrackIds();
  getController().setPendingCommandTrack(launchpad_manager_->resolveTrackId(device_id, track_ids, pattern_editor_->getCursorTrackIndex()));

  // Pure per-device commands (octave/track-follow - no Song/Track access,
  // no keyboard/M-x equivalent) go through LaunchpadManager's own entry
  // point first; everything else (Song/Track-mutating commands like
  // "toggle-mute", or anything else registered anywhere) falls through to
  // the exact same executeCommand() a keybinding or M-x invocation uses.
  // Deliberately bypassing active_element_/Controller::sendCommand's focus
  // routing either way, to match how pad input already reaches
  // PatternEditor unconditionally (see handleLaunchpadPadEvent above) -
  // these would otherwise silently no-op whenever some other window
  // happens to have focus.
  bool handled = launchpad_manager_->handleCommand(*name, device_id, pattern_editor_->getCursorTrackIndex(), static_cast<int>(track_ids.size()));
  if (!handled) handled = executeCommand(*name);

  getController().setPendingCommandTrack(-1);
}

static void audio_thread_func(Controller * controller, AudioAPI * audio) {
  Player player(controller->getChannelConfiguration(), controller);
  player.play(*audio);
}

static void visualization_thread_func(Controller * controller, int sample_rate, int frame_count) {
  VisualizationThread visualization_thread(controller);
  visualization_thread.configure(sample_rate, frame_count);
  visualization_thread.run();
}

void
UI::start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager) {
  launchpad_manager.setLaunchpadIO(&launchpad_io);
  launchpad_manager_ = &launchpad_manager;
  // "move-row-up"/"move-row-down" while in GridMode::SESSION move
  // PatternMatrix's own scene cursor instead of scrolling a pad-grid row
  // window - see LaunchpadManager::session_move_scene_callback_'s own
  // comment for why.
  launchpad_manager.setSessionMoveSceneCallback([this](int delta) { pattern_matrix_->moveCursorScene(getController().getSong(), delta); });
  // "next-track"/"prev-track" outside GridMode::SESSION move the one
  // shared cursor every connected Launchpad follows - see
  // LaunchpadManager::track_move_callback_'s own comment for why.
  launchpad_manager.setTrackMoveCallback([this](int new_track_index) { pattern_editor_->setCursorTrack(new_track_index); });

  std::thread audio_thread(audio_thread_func, &(getController()), &audio);
  std::thread visualization_thread(visualization_thread_func, &(getController()), audio.getFrequency(), audio.getFrameCount());

  startUI(audio, launchpad_io);

  getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::TERMINATE));
  getController().getVisualizationQueue().push(make_unique<AudioBlockEvent>(AudioBuffer(), AudioBuffer(), AudioBuffer(), AudioBuffer()));

  audio_thread.join();
  visualization_thread.join();
}

void
StatusLogger::log(std::string s) {
  ui_->setStatus(std::move(s));
}
