#include "TerminalUI.h"

#include "../../playback/InputEvent.h"
#include "../../Controller.h"
#include "../UIMenu.h"
#include "../CommandDescriptions.h"
#include "../HeatmapChart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "PatternEditor.h"
#include "ArrangementGrid.h"
#include "ClipGrid.h"
#include "ChannelMeter.h"
#include "SpectrumMeter.h"
#include "OutlineView.h"
#include "CoverArt.h"
#include "SpinBox.h"
#include "../../dsp/DiracAnalyzer.h"
#include "../../audio/AudioAPI.h"
#include "../../launchpad/LaunchpadIO.h"
#include "../../midi/MidiInput.h"
#include "../../util/WaitSet.h"
#include "../../launchpad/LaunchpadPadEvent.h"
#include "../../launchpad/LaunchpadButtonEvent.h"
#include "../../launchpad/LaunchpadChannelPressureEvent.h"
#include "../../launchpad/LaunchpadProtocol.h"
#include "../../launchpad/LaunchpadManager.h"
#include "SubcellGlyphs.h"
#include "../../util/Utf8.h"
#include "../../util/constants.h"
#include "../../model/Color.h"
#include "../KeyChord.h"
#include "../../playback/Player.h"
#include "../../playback/PlaybackEvent.h"
#include "../../playback/LogEvent.h"
#include "../../playback/RecordEvent.h"
#include "../../playback/RecordingLatencyEvent.h"
#include "../../model/ArrangementOps.h"
#include "../../audio/AudioBuffer.h"
#include "../../playback/ThresholdRecordingTriggeredEvent.h"
#include "../../playback/PlaybackControlEvent.h"
#include "../../playback/AudioBlockEvent.h"
#include "../../playback/VisualizationResultEvent.h"
#include "../../playback/VisualizationThread.h"
#include "../../bus/BusEffectRegistry.h"

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <clocale>
#include <cassert>
#include <unistd.h>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#include <array>
#include <filesystem>
#include <thread>
#include <fmt/core.h>

#include <sys/time.h>

#include <cellui/MenuBar.h>
#include <cellui/KeyReader.h>
#include <cellui/Scopes.h>
#include <cellui/Backend.h>

#include <poll.h>


using namespace std;

namespace {
// SYNTH_UI_STATS=<file>: see startUI().
std::map<std::string, int> g_ui_stats;
bool g_ui_stats_on = false;
void statCount(const std::string & what) { if (g_ui_stats_on) g_ui_stats[what]++; }

// Every thread's CPU time so far in clock ticks, by thread name.
std::map<std::string, long> threadCpuTicks() {
  std::map<std::string, long> ticks;
  std::error_code ec;
  for (auto & entry : std::filesystem::directory_iterator("/proc/self/task", ec)) {
    FILE * f = fopen((entry.path() / "stat").c_str(), "r");
    if (!f) continue;
    char line[1024];
    size_t n = fread(line, 1, sizeof line - 1, f);
    fclose(f);
    line[n] = 0;
    // pid (comm) state ppid ... utime stime: fields 14 and 15.
    char *open = strchr(line, '('), *close = strrchr(line, ')');
    if (!open || !close) continue;
    long utime = 0, stime = 0;
    if (sscanf(close + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &utime, &stime) != 2) continue;
    ticks[std::string(open + 1, close)] += utime + stime;
  }
  return ticks;
}
}
using namespace fmt;

static inline long long now() {
  struct timeval tv;
  int r = gettimeofday(&tv, 0);
  if (r == 0) {
    return (long long)1000 * tv.tv_sec + tv.tv_usec / 1000;
  } else {
    return 0;
  }
}

// One declarative source for every section/item, rather than hand-written
// item arrays plus a separately-maintained desc->command map - adding an
// item is one line here, not several coordinated edits. `label == nullptr`
// is a separator (MenuBar draws it as a divider); `binding` is the
// human-readable keybinding shown right-aligned against the item box's
// widest label, e.g. "Save    C-x C-s" - an item has no shortcut of its own
// (one would render as a single bare
// character with no modifier indication, misleading for anything but a
// plain unmodified key, which is nearly nothing real this app binds).
// Section headers keep real single-letter Alt+<mnemonic> shortcuts - a
// bare Alt+letter has no such ambiguity.
struct MenuItemSpec {
  const char * label;    // nullptr = separator
  const char * binding;  // human-readable keybinding shown next to label; "" = none
  const char * command;  // name passed to Controller::sendCommand() on activation
  const char * desktop_binding = nullptr; // shown instead of `binding` on a display using the desktop keymap
};
struct MenuSectionSpec {
  const char * name;
  char mnemonic;          // Alt+<mnemonic> opens this section
  vector<MenuItemSpec> items;
};

// The Buffers section's own item list depends on which songs are actually
// open, unlike every other (fixed) section above, so this now builds a
// fresh vector on every call instead of handing back one cached forever -
// see TerminalMenu::rebuild(), which builds `buffer_items` (its own label/
// command strings backed by rebuild()'s local variables, not this
// function's) and passes it straight through here.
static vector<MenuSectionSpec> menuSpec(vector<MenuItemSpec> buffer_items) {
  vector<MenuSectionSpec> spec = {
      {"File", 'f', {
                        {"Open...", "C-x C-f", "open-song", "C-o"},
                        {"Save", "C-x C-s", "save-song", "C-s"},
                        {"Save As...", "C-x C-w", "save-song-as", "C-S-S"},
                        {nullptr, nullptr, nullptr},
                        {"Quit", "C-x C-c", "save-buffers-kill-terminal", "C-q"},
                    }},
      {"Edit", 'e', {
                        {"Undo", "C-x u", "undo", "C-z"},
                        {"Redo", "C-M-_", "undo-redo", "C-S-Z"},
                        {nullptr, nullptr, nullptr},
                        {"Select All", "C-x h", "mark-whole-buffer"},
                        {"Set Mark", "C-SPC", "set-mark"},
                        {"Kill Region", "C-w", "kill-region", "C-x"},
                        {"Copy", "M-w", "kill-ring-save", "C-c"},
                        {"Yank", "C-y", "yank", "C-v"},
                        {"Cancel", "C-g", "keyboard-quit"},
                        {nullptr, nullptr, nullptr},
                        {"Transpose Up", "C-S-Up", "transpose-region-up"},
                        {"Transpose Down", "C-S-Down", "transpose-region-down"},
                        {"Humanize", "", "humanize-region"},
                        {nullptr, nullptr, nullptr},
                        {"Apply Just Intonation", "", "apply-just-intonation-region"},
                        {"Clear Tuning Corrections", "", "clear-tuning-correction-region"},
                    }},
      {"Track", 't', {
                         {"Add Instrument Track", "C-t", "add-instrument-track"},
                         {"Add Percussion Track", "C-S-D", "add-percussion-track"},
                         {"Add Sample Track", "C-r", "add-sample-track"},
                         {"Add Group Track", "", "add-group-track"},
                         {"Rename Track...", "F2", "rename-track"},
                         {"Delete Track", "", "delete-track"},
                         {nullptr, nullptr, nullptr},
                         {nullptr, nullptr, nullptr},
                         {"Toggle Mute", "\\", "toggle-mute"},
                         {"Toggle Solo", "C-\\", "toggle-solo"},
                         {"Cycle Monitor (Auto/In/Off)", "", "cycle-monitor"},
                         {nullptr, nullptr, nullptr},
                         {"Add Note Column", "C-S-Right", "add-note-column"},
                         {"Remove Note Column", "C-S-Left", "remove-note-column"},
                     }},
      // Clip playback and clip content. Launching is quantized to the bar,
      // like a Launchpad Live-View pad.
      {"Clip", 'c', {
                        {"Launch Clip", "", "launch-clip"},
                        {"Launch Scene", "", "launch-scene"},
                        {"Stop All Clips", "", "stop-all-clips"},
                        {"Back to Arrangement", "", "back-to-arrangement"},
                        {"Track Back to Arrangement", "", "track-back-to-arrangement"},
                        {nullptr, nullptr, nullptr},
                        {"New Clip from Selection", "", "copy-to-clip"},
                        {"Merge Clip to Background", "C-x m", "merge-clip-to-background"},
                        {nullptr, nullptr, nullptr},
                        {"Copy Clip", "M-w", "kill-ring-save", "C-c"},
                        {"Kill Clip", "C-w", "kill-region", "C-x"},
                        {"Yank Clip", "C-y", "yank", "C-v"},
                        {"Quantize Clip", "", "quantize-clip"},
                        {"Double Clip Length", "", "double-clip-length"},
                        {"Halve Clip Length", "", "halve-clip-length"},
                        {"Toggle Stop Button", "", "toggle-stop-button"},
                        {"Rename Clip...", "", "rename-clip"},
                    }},
      // Song-wide settings.
      {"Song", 's', {
                        {"Toggle Binaural Mixer", "", "toggle-mixer-type"},
                        {nullptr, nullptr, nullptr},
                        {"Set Song Key...", "", "set-song-key"},
                        {"Set Scale...", "", "set-song-scale"},
                        {"Set Tuning System...", "", "set-song-tuning"},
                        {"Set Time Signature...", "", "set-time-signature"},
                        {nullptr, nullptr, nullptr},
                        {"Set Bus Effect A...", "", "set-bus-effect-a"},
                        {"Set Bus Effect B...", "", "set-bus-effect-b"},
                    }},
      // How the active song is shown (UI::View) - not a buffer of its own.
      {"View", 'v', {
                        {"Arrangement View", "", "arrangement-view"},
                        {"Live View", "", "live-view"},
                        {"Toggle Arrangement/Live", "TAB", "toggle-view"},
                        {nullptr, nullptr, nullptr},
                        {"Outline", "", "outline-view"},
                        {"Toggle Outline", "", "toggle-outline"},
                        {"Toggle Scopes", "", "toggle-scopes"},
                    }},
      // Machine-wide, not per song - the choice is remembered across runs.
      {"Devices", 'd', {
                           {"Select Audio Input...", "", "select-capture-device"},
                           {"Select Audio Output...", "", "select-playback-device"},
                           {"Select MIDI Input...", "", "select-midi-input"},
                       }},
  };

  // Emacs-style Buffers menu: every open buffer's name (already display-
  // formatted - basename plus an active-buffer marker, each item's own
  // command already a specific "switch-to-buffer:<full name>" - by
  // rebuild() below; see its own comment for why the label can't just be
  // the command name with a prefix stripped), then the real
  // buffer-management commands. "New" has no separate entry here - see
  // select-named-buffer's own comment in UI.cpp for why.
  buffer_items.push_back({ nullptr, nullptr, nullptr });
  buffer_items.push_back({ "Kill Buffer", "C-x k", "kill-buffer" });
  buffer_items.push_back({ "Next Buffer", "C-x Right", "next-buffer" });
  buffer_items.push_back({ "Previous Buffer", "C-x Left", "previous-buffer" });
  buffer_items.push_back({ "Select Named Buffer...", "C-x b", "select-named-buffer" });
  spec.push_back({ "Buffers", 'b', std::move(buffer_items) });
  spec.push_back({ "Help", 'h', {
      { "About", "", "about" },
    } });

  return spec;
}

// The menu bar: menuSpec()'s sections on a MenuBar in a plane of its own
// over the whole screen. A menu item has only display text, so this maps an
// activated item to its command.
class TerminalMenu : public UIMenu {
public:
  TerminalMenu(UIPlane & parent, vector<string> buffer_names, vector<string> buffer_display_names, string active_buffer_name)
    : parent_(parent), plane_(parent.createChild()), bar_(*plane_), buffer_names_(std::move(buffer_names)),
      buffer_display_names_(std::move(buffer_display_names)), active_buffer_name_(std::move(active_buffer_name)) {
    // The plane spans the tallest section; below the bar and an open
    // section the screen shows through.
    plane_->makeTransparent();
    rebuild();
  }

  void applyStyles(const StyleProvider & styles) override {
    cellui::MenuBar::Colors colors;
    colors.header_fg = styles.menu_fg_color;
    colors.header_bg = styles.menu_bg_color;
    colors.section_fg = styles.menu_section_fg_color;
    colors.section_bg = styles.menu_section_bg_color;
    bar_.setColors(colors);
    bar_.draw();
  }

  void refreshBuffers(const vector<string> & buffer_names, const vector<string> & buffer_display_names,
		       const string & active_buffer_name) override {
    buffer_names_ = buffer_names;
    buffer_display_names_ = buffer_display_names;
    active_buffer_name_ = active_buffer_name;
    rebuild();
  }

  bool offerInput(const cellui::Input & input) override {
    if (!bar_.offerInput(input)) return false;
    if (auto activated = bar_.takeActivated()) {
      activated_command_ = commands_[static_cast<size_t>(activated->first)][static_cast<size_t>(activated->second)];
    }
    return true;
  }

  std::string takeActivatedCommand() override {
    string c = std::move(activated_command_);
    activated_command_.clear();
    return c;
  }

  // Also follows the screen's width, which a resize changes.
  void raiseToTop() override {
    if (parent_.getDim().second != cols_) {
      cols_ = parent_.getDim().second;
      bar_.layout(cols_);
    }
    plane_->moveToTop();
  }

  bool isOpen() const override { return bar_.isOpen(); }

private:
  // Every buffer item's label is its display name (Controller::
  // getBufferDisplayName()'s disambiguated text, from the caller), prefixed
  // with "* " for the active one; its command is "switch-to-buffer:" plus
  // the full name, so an item reaches that buffer even when two show alike.
  void rebuild() {
    vector<string> display_labels, commands;
    for (size_t i = 0; i < buffer_names_.size(); i++) {
      auto & name = buffer_names_[i];
      auto & display_name = i < buffer_display_names_.size() ? buffer_display_names_[i] : name;
      display_labels.push_back((name == active_buffer_name_ ? "* " : "  ") + display_name);
      commands.push_back("switch-to-buffer:" + name);
    }
    vector<MenuItemSpec> buffer_items;
    for (size_t i = 0; i < buffer_names_.size(); i++) {
      buffer_items.push_back({ display_labels[i].c_str(), "", commands[i].c_str() });
    }
    auto spec = menuSpec(std::move(buffer_items));

    // An item's binding is shown right-aligned after the section's widest
    // label.
    vector<cellui::MenuBar::Section> sections;
    commands_.clear();
    bool desktop = parent_.keymapProfile() == cellui::KeymapProfile::DESKTOP;
    for (auto & section : spec) {
      size_t label_width = 0;
      for (auto & item : section.items) {
	if (item.label) label_width = std::max(label_width, static_cast<size_t>(Utf8::displayWidth(item.label)));
      }
      cellui::MenuBar::Section out;
      out.name = section.name;
      out.mnemonic = static_cast<unsigned char>(section.mnemonic);
      vector<string> section_commands;
      for (auto & item : section.items) {
	cellui::MenuBar::Item entry;
	if (!item.label) {
	  entry.separator = true;
	  section_commands.emplace_back();
	} else {
	  entry.label = item.label;
          const char * binding = desktop && item.desktop_binding ? item.desktop_binding : item.binding;
          if (binding && binding[0]) {
            entry.label.append(label_width + 2 - static_cast<size_t>(Utf8::displayWidth(item.label)), ' ');
            entry.label += binding;
          }
          section_commands.emplace_back(item.command ? item.command : "");
	}
	out.items.push_back(std::move(entry));
      }
      sections.push_back(std::move(out));
      commands_.push_back(std::move(section_commands));
    }
    bar_.setSections(std::move(sections));
    cols_ = parent_.getDim().second;
    bar_.layout(cols_);
  }

  UIPlane & parent_;
  unique_ptr<UIPlane> plane_;
  cellui::MenuBar bar_;
  int cols_ = 0;
  vector<string> buffer_names_;
  vector<string> buffer_display_names_;
  string active_buffer_name_;
  vector<vector<string>> commands_;  // by section, then item
  string activated_command_;
};

TerminalUI::TerminalUI(cellui::Backend & backend)
    : backend_(backend), screen_(backend.rows(), backend.cols()) {
  // Something that closes on a bare Escape gets it at once, rather than when
  // the next key shows it was no Alt prefix.
  key_reader_.takeEscape = [this](const cellui::Input & escape) {
    auto active = outline_view_ && outline_view_->isModal() ? outline_view_ : active_element_.lock();
    if (command_palette_.isOpen()) {
      command_palette_.close();
      force_next_render_ = true;
    } else if (choice_dialog_.isOpen()) {
      closeChoiceDialog();
    } else if (info_dialog_.isOpen()) {
      closeInfoDialog();
    } else if (active && active->wantsBareEscape()) {
      active->offerInput(escape);
    } else {
      return false;
    }
    return true;
  };
}

TerminalUI::~TerminalUI() {
}

// Shows "ESC-" once a pending Escape has waited KeyReader::kIndicatorDelay,
// the way Emacs shows its Meta prefix; readInput() clears it when the wait
// ends.
void
TerminalUI::updateEscapeIndicator() {
  auto due = key_reader_.indicatorDue();
  if (!due || escape_indicator_shown_) return;
  if (std::chrono::steady_clock::now() >= *due) {
    setStatus("ESC-");
    escape_indicator_shown_ = true;
  }
}

// The loop's wait, shortened so the indicator appears on time.
int
TerminalUI::escapeIndicatorPollTimeoutMs() const {
  constexpr int kDefaultPollTimeoutMs = 1000;
  auto due = key_reader_.indicatorDue();
  if (!due || escape_indicator_shown_) return kDefaultPollTimeoutMs;
  auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(*due - std::chrono::steady_clock::now()).count();
  return static_cast<int>(std::clamp<long long>(remaining_ms, 0, kDefaultPollTimeoutMs));
}

void
TerminalUI::initialize(std::shared_ptr<Controller> & controller) {
  const auto & display = backend_.display();
  bool use_pixel = display.pictures && display.cell_px_height > 0 && display.cell_px_width > 0;
  auto root_plane = make_unique<cellui::BufferPlane>(screen_, styles_, controller);
  root_plane_ = root_plane.get();
  if (display.default_fg && display.default_bg) root_plane_->setDefaultColors(*display.default_fg, *display.default_bg);
  root_plane_->setDisplayScale(display.display_scale);
  root_plane_->setSextants(display.sextants);
  root_plane_->setSmoothScroll(display.smooth_scroll);
  root_plane_->setKeymapProfile(display.keymap_profile);
  key_reader_.setEscapeAsMeta(display.escape_as_meta);
  root_plane_->setFlatChildren(true);
  root_plane_->setThemedBases(true);
  if (use_pixel) root_plane_->setCellPixelSize(display.cell_px_height, display.cell_px_width);
  setPlane(std::move(root_plane));
  // The modal dialogs float above every overlay (menus, pickers, popups).
  choice_dialog_.setElevation(styles_.theme.elevation.modal);
  info_dialog_.setElevation(styles_.theme.elevation.modal);

  setFgColor(styles_.window_fg_color);
  setBgColor(styles_.window_bg_color);
  fill();

  {
    auto buffer_names = controller->getBufferNames();
    vector<string> buffer_display_names;
    buffer_display_names.reserve(buffer_names.size());
    for (auto & name : buffer_names) buffer_display_names.push_back(controller->getBufferDisplayName(name));
    menu_ = make_shared<TerminalMenu>(getPlane(), buffer_names, std::move(buffer_display_names), controller->getActiveBufferName());
    menu_->applyStyles(styles_);
  }

  scopes_ = cellui::scopesFor(display);
  chart_ = scopes_->spectrum(getPlane());
  volume_meter_ = scopes_->channelMeter(getPlane());
  heatmap_ = scopes_->heatmap(getPlane(), DiracAnalyzer::kAzimuthBins, DiracAnalyzer::kElevationBins);

  // No footer legend on either scope: the heatmap's axis extremes are
  // drawn directly in the grid area instead (axisLabels(), above), and the
  // FFT chart's "Spectrum: 0-{nyquist}Hz" label was removed the same way,
  // reclaiming the row a legend would have used.

  initializeWidgets();

  // Live View by default (the --view option); setView() lays out and
  // moves focus to the view's own widget itself.
  if (initial_view_ == View::LIVE) setView(View::LIVE);
  else layout();

  render();
}

// Every other widget was created after menu_, and layout() can raise a
// widget above its header row, so it is raised before every frame (its open
// section floats at the overlay elevation over everything but a modal
// dialog) - and an open modal dialog above its scrim, which share the modal
// elevation.
void TerminalUI::raiseOverlays() {
  menu_->raiseToTop();
  modal_scrim_.raise();
  command_palette_.raise();
  choice_dialog_.raise();
  info_dialog_.raise();
  // An info dialog takes no clicks: the terminal selects its text instead
  // (the blurred screen behind it has none to select).
  backend_.setNativeSelection(info_dialog_.isOpen());
}

void
TerminalUI::render() {
  raiseOverlays();
  root_plane_->refresh();
  cellui::CursorRequest cursor;
  if (auto at = root_plane_->cursorPosition()) cursor = { true, at->first, at->second };
  cellui::FrameInfo info;
  info.backdrops = root_plane_->takeBackdropLayers();
  info.elevations = root_plane_->takeElevations();
  info.mouse_cursors = root_plane_->takeMouseCursors();
  if (backend_.wantsFrameInfo()) {
    info.planes = root_plane_->describe();
    info.focus = focusName();
  }
  backend_.present(screen_, cursor, root_plane_->images(), &info);
}

void
TerminalUI::step() {
  readInput();
  while (getController().getUIEventQueue().hasEvents()) {
    auto event = getController().getUIEventQueue().pop();
    handleEvent(*event);
  }
  renderComponents();
  render();
}

std::string
TerminalUI::focusName() const {
  if (command_palette_.isOpen()) return "command-palette";
  if (choice_dialog_.isOpen()) return "choice-dialog";
  if (info_dialog_.isOpen()) return "info-dialog";
  if (outline_view_ && outline_view_->isModal()) return "outline-view";
  auto active = active_element_.lock();
  if (!active) return "";
  if (active == pattern_editor_) return "pattern-editor";
  if (active == arrangement_grid_) return "arrangement-grid";
  if (active == clip_grid_) return "clip-grid";
  if (active == outline_view_) return "outline-view";
  if (active == status_line_) return "status-line";
  return "other";
}

void
TerminalUI::resizeScreen() {
  if (screen_.rows() == backend_.rows() && screen_.cols() == backend_.cols()) return;
  screen_ = cellui::CellBuffer(backend_.rows(), backend_.cols());
  root_plane_->resize(backend_.rows(), backend_.cols());
}

// The command row has no background of its own: the display cannot draw the
// last cell of the last row, so a coloured row would end in a different one.
void
TerminalUI::clearLastRow() {
  auto [rows, cols] = getDim();
  if (rows < 1) return;
  setBgDefault();
  putstr(rows - 1, 0, std::string(static_cast<size_t>(cols), ' '));
}

void
TerminalUI::applyTheme(const Theme & theme) {
  styles_.applyTheme(theme);
  if (!hasPlane()) return;
  setFgColor(styles_.window_fg_color);
  setBgColor(styles_.window_bg_color);
  fill();
  clearLastRow();
  if (menu_) menu_->applyStyles(styles_);
  scopes_stale_ = true;
  force_next_render_ = true;
}

void TerminalUI::setReducedMotion(bool reduced) {
  styles_.reduced_motion = reduced;
  force_next_render_ = true;
}

void
TerminalUI::refresh() {
  backend_.redraw();
}

bool
TerminalUI::readInput() {
  using cellui::BackendEvent;
  for (auto & event : backend_.poll()) {
    switch (event.type) {
      case BackendEvent::Type::INPUT:
        key_reader_.push(event.input);
        break;
      case BackendEvent::Type::RESIZE:
        resizeScreen();
        key_reader_.push(cellui::Input(Key::Resize, -1, -1, false, false, false, false));
        break;
      case BackendEvent::Type::PASTE:
        // Pasted text goes into an open text field; elsewhere it is not
        // typed as keys, which would play it as notes and commands.
        root_plane_->insertIntoReader(event.text);
        break;
      case BackendEvent::Type::DISPLAY:
        // What a colour looks like depends on the display.
        force_next_render_ = true;
        break;
      case BackendEvent::Type::CLOSE:
        close_ui_ = true;
        break;
      default:
        break;
    }
  }

  cellui::Input input(0, -1, -1, false, false, false, false);
  while (key_reader_.next(&input)) offerInput(input);
  if (escape_indicator_shown_ && !key_reader_.escapePending()) {
    setStatus("");
    escape_indicator_shown_ = false;
  }
  return true;
}

void
TerminalUI::commitOverviewCell(int track_id, int row) {
  // The whole commit refuses while playing - matches PatternEditor's own
  // move-row-up/move-row-down guard (row navigation only ever runs while
  // stopped), and avoids a commit that silently only did half of what it
  // normally does (move the track but not the playhead) while playing.
  if (getController().getPlaybackInfo().isPlaying()) return;

  auto & song = getController().getSong();
  getController().setEditPosition(row);

  auto track_ids = pattern_editor_->trackIds(song);
  auto it = find(track_ids.begin(), track_ids.end(), track_id);
  if (it != track_ids.end()) {
    pattern_editor_->setCursorTrack(static_cast<int>(it - track_ids.begin()));
  }
  active_element_ = pattern_editor_;
}

void
TerminalUI::initializeWidgets() {
  // chart and volume are missing
  pattern_editor_ = make_shared<PatternEditor>(getPlane());
  pattern_editor_->setScopes(scopes_.get());
  arrangement_grid_ = make_shared<ArrangementGrid>(getPlane());
  clip_grid_ = make_shared<ClipGrid>(getPlane());
  clip_grid_->setScopes(scopes_.get());
  outline_view_ = make_shared<OutlineView>(getPlane());
  // Enter commits the cell under this grid's own (local, passive) cursor
  // to shared state - see ArrangementGrid.h's own comment on why this is a
  // callback rather than the grid reaching for PatternEditor/
  // active_element_ itself (it doesn't know either exists).
  // launchpad_manager_ isn't set yet at this point (wireLaunchpad() assigns
  // it later, after main.cpp's own ui.initialize()/ui.start() call order -
  // see that method for the equivalent Launchpad wiring), so that half of
  // commitOverviewCell()'s callers is wired there instead.
  arrangement_grid_->setCommitCallback([this](int track_id, int row) { commitOverviewCell(track_id, row); });
  cover_art_ = make_shared<CoverArt>(getPlane());
  info_line_ = make_shared<InfoLine>(getPlane());
  status_line_ = make_shared<StatusLine>(getPlane());
  // See StatusLine::showPrompt()'s own comment: opening any StatusLine
  // reader (M-x included) must take focus away from any widget's own
  // inline editor rather than opening on top of it.
  status_line_->setCommandPaletteCallback([this]() { showCommandPalette(); });
  status_line_->setBeforeShowPromptCallback([this]() {
    pattern_editor_->cancelReaderEdit();
    clip_grid_->cancelReaderEdit();
  });
  // The info bar's own colors - this widget sits inline in that bar (see
  // layout()), and must blend into it.
  octave_control_ = make_shared<SpinBox>(getPlane(), "Octave:", constants::MIN_OCTAVE, constants::MAX_OCTAVE,
    [this] { return getController().getGlobalOctave(); },
    [this](int v) { getController().setGlobalOctave(v); },
    styles_.info_line_bg_color, styles_.info_line_fg_color);

  // An overview first, not pattern editing - the arrangement grid when
  // starting in Arrangement view; initialize() moves it to the clip grid
  // when starting in Live View.
  active_element_ = arrangement_grid_;

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
  // toggle-playing/octave-up/octave-down/save-song are UI's own now (plain
  // Controller calls, no widget dependency) - octave_control_'s own [-]/[+]
  // buttons call the exact same Controller methods octave-up/-down do.
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

  // The song's time signature, which the arrangement counts its bars in.
  // Prefilled with the current one, so Enter alone changes nothing.
  commands_.define("set-time-signature", [this]() {
    status_line_->showPrompt("Time signature (n/d): ", [this](const std::string & typed) {
      if (typed.empty()) return;
      auto signature = TimeSignature::parse(typed);
      if (!signature || !signature->isSet()) { setStatus("Not a time signature: " + typed); return; }
      auto & song = getController().getSong();
      Song::Edit edit(song, "set time signature");
      song.setTimeSignature(*signature);
      setStatus("Time signature " + signature->toString()); }, getController().getSong().getTimeSignature().toString());
  });

  // C-x C-x (exchange-point-and-mark) is a PatternEditor-owned command (the
  // mark/point state it swaps lives there, alongside set-mark/kill-region -
  // see PatternEditor.cpp's own definition) - forwarded through
  // executeCommand() the same way this class's own active-element fallback
  // would, since the C-x prefix itself is only ever recognized at this
  // level (dispatchCommand() below checks *this* registry, not
  // PatternEditor's - unlike the M-x path, which does go through that
  // fallback chain).
  commands_.define("exchange-point-and-mark", [this]() { pattern_editor_->executeCommand("exchange-point-and-mark"); });
  // Same reasoning: the C-x prefix is only recognized here, but the whole
  // song is selected in the pattern editor.
  commands_.define("mark-whole-buffer", [this]() { pattern_editor_->executeCommand("mark-whole-buffer"); });
  // merge-clip-to-background/toggle-record-arm are UI's own now (plain
  // Controller::sendCommand() forwarding, same shape save-song's own
  // C-x-reachable wrapper uses) - both target Song::getCurrentTrackId(),
  // not any one widget's own cursor, so they work regardless of which UI
  // widget currently has focus.

  // other-window (C-x o): cycles focus through the current view's
  // focusable widgets (focusableElements()).
  commands_.define("other-window", [this]() {
    auto elements = focusableElements();
    auto it = std::find(elements.begin(), elements.end(), active_element_.lock());
    if (it != elements.end()) ++it;
    active_element_ = it == elements.end() ? elements.front() : *it;
  });
  // Live View: launch the pattern editor's current clip (the current
  // track's clip in its scene), or every track's clip in that scene -
  // quantized to the bar, exactly like a Launchpad Live-View pad or
  // scene button.
  commands_.define("launch-clip", [this]() {
    if (!pattern_editor_->isLiveMode()) return;
    auto track_id = getController().getSong().getCurrentTrackId();
    getController().getClipPlayer().triggerClip(track_id, pattern_editor_->getLiveScene(track_id));
  });
  commands_.define("launch-scene", [this]() {
    if (!pattern_editor_->isLiveMode()) return;
    auto & song = getController().getSong();
    getController().getClipPlayer().launchScene(pattern_editor_->getLiveScene(song.getCurrentTrackId()), song.getPlayableTrackIds());
  });
  commands_.define("stop-all-clips", [this]() {
    getController().getClipPlayer().stopAllTracks();
  });
  // Arrangement view's scope row (cover art, ArrangementGrid, charts), and
  // Live View's spectrum/heatmap under the outline panel.
  commands_.define("toggle-scopes", [this]() {
    scopes_visible_ = !scopes_visible_;
    viewChanged();
  });
  // Live View with the outline panel shown and focused - defined here,
  // not with the other view commands in UI.cpp, since it focuses a
  // concrete widget.
  commands_.define("outline-view", [this]() {
    setOutlineVisible(true);
    setView(View::LIVE);
    active_element_ = outline_view_;
  });

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
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('c', true, false, false, false), "save-buffers-kill-terminal");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('s', true, false, false, false), "save-song");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('f', true, false, false, false), "open-song");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('w', true, false, false, false), "save-song-as");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('x', true, false, false, false), "exchange-point-and-mark");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('h', false, false, false, false), "mark-whole-buffer");
  // The buffer commands' own real Emacs bindings, unlike every C-x C-<letter>
  // above, hold Ctrl for the C-x prefix only, not the second key: kill-buffer
  // is C-x k (plain k), next-buffer/previous-buffer are C-x <right>/C-x
  // <left>, and select-named-buffer (switch-to-buffer) is C-x b.
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('k', false, false, false, false), "kill-buffer");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack(Key::Right, false, false, false, false), "next-buffer");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack(Key::Left, false, false, false, false), "previous-buffer");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('b', false, false, false, false), "select-named-buffer");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('o', false, false, false, false), "other-window");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('m', false, false, false, false), "merge-clip-to-background");
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('r', false, false, false, false), "toggle-record-arm");
  // Emacs's own undo bindings: C-x u, plus C-_ and C-/ (a terminal sends the
  // same byte for both). undo-redo (Emacs 28) is on C-M-_.
  keymap_.bindPrefixed(KeymapProfile::EMACS, ctrl_x, KeyChord::pack('u', false, false, false, false), "undo");
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack('_', true, false, false, false), "undo");
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack('/', true, false, false, false), "undo");
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack('_', true, true, false, false), "undo-redo");
  // What terminals actually send for them: the single control byte 0x1f
  // (older builds reported it as key 31, no modifiers; with Alt in front for
  // the redo; the parser now decodes it as Ctrl+_), and in the Kitty protocol Ctrl+Shift+- for C-_.
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack(31, false, false, false, false), "undo");
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack(31, false, true, false, false), "undo-redo");
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack('-', true, false, true, false), "undo");
  keymap_.bind(KeymapProfile::EMACS, KeyChord::pack('-', true, true, true, false), "undo-redo");
  // The desktop ones.
  // File commands, with the C-x prefix Emacs-only (Ctrl+X is cut here).
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('s', true, false, false, false), "save-song");
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('s', true, false, true, false), "save-song-as");
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('o', true, false, false, false), "open-song");
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('q', true, false, false, false), "save-buffers-kill-terminal");
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('z', true, false, false, false), "undo");
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('z', true, false, true, false), "undo-redo");
  keymap_.bind(KeymapProfile::DESKTOP, KeyChord::pack('y', true, false, false, false), "undo-redo");
  keymap_.bind(KeyChord::pack(' ', false, false, false, false), "toggle-playing");
  keymap_.bind(KeyChord::pack('[', false, false, false, false), "octave-down");
  keymap_.bind(KeyChord::pack(']', false, false, false, false), "octave-up");
  keymap_.bind(KeyChord::pack('\t', false, false, false, false), "toggle-view");
  // Track creation works from every widget, the same as its Track menu
  // entries. Ctrl+Shift+D ("Drum") because plain Ctrl-D is taken.
  keymap_.bind(KeyChord::pack('t', true, false, false, false), "add-instrument-track");
  keymap_.bind(KeyChord::pack('r', true, false, false, false), "add-sample-track");
  keymap_.bind(KeyChord::pack('d', true, false, true, false), "add-percussion-track");

  assertCommandBindingsValid();

  // Lets StatusLine's M-x path (Controller::sendCommand, which only reaches
  // Controller/Song-level state) also invoke commands owned by this class or
  // by whichever widget is currently active, without Controller depending
  // on any UI type - see Controller.h's command_fallback_.
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

    // Cursor/scroll/selection/live-note/locator-editing state - see
    // PatternEditor::handleBufferChanged()'s own comment. The view (UI::
    // View) is untouched: a buffer switch changes which song is shown, not
    // how.
    pattern_editor_->handleBufferChanged();
  });
}

bool
TerminalUI::executeCommand(std::string_view name) {
  // A command reached here other than through M-x's own submission (a menu
  // click, Launchpad-by-name dispatch) always takes focus away from
  // whatever a status-line prompt was reading, same as StatusLine::cancelReader()'s own
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
  // offerInput()'s BUTTON1 handling already establishes: a click that
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
TerminalUI::commandCompletions(std::string_view prefix) const {
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
TerminalUI::layout() {
  auto [ rows, cols ] = getDim();
  outline_view_->setScreenSize(rows, cols);

  constexpr int kHeatmapWidth = 31; // 20 * 1.5, rounded up to the nearest odd width
  constexpr int kScopeHeight = 5;
  // Arrangement view shows the scope row unless toggle-scopes hid it
  // (Live View's own scopes sit in its left column, below). A hidden
  // row's widgets move below the screen rather than shrinking away (see
  // the workspace comment below) - resize() tears down a chart's plot
  // plane, so the next one is built there too, off screen.
  bool show_scopes = getView() == View::ARRANGEMENT && scopes_visible_;
  int scope_row = 1;
  cover_art_->setVisible(show_scopes);
  arrangement_grid_->setVisible(show_scopes);
  volume_meter_->setVisible(show_scopes);

  constexpr int kOutlineWidth = 30;
  // Live View: the outline panel runs the full workspace height, with
  // the spectrum and heatmap stacked under it (and a divider row above
  // each). Dropped when the terminal is too short to leave the outline a
  // usable height.
  int live_outline_cols = isOutlineVisible() ? std::min(kOutlineWidth, cols / 2) : 0;
  int live_scope_rows = 2 * kScopeHeight + 2;
  bool live_scopes = getView() == View::LIVE && scopes_visible_ && live_outline_cols > 0
    && std::max(2, rows - 3) - live_scope_rows >= 6;
  const bool scopes_were_on_screen = scopes_on_screen_;
  scopes_on_screen_ = show_scopes || live_scopes;
  if (scopes_on_screen_ && !scopes_were_on_screen) scopes_stale_ = true;

  // cover_art_ claims the scope row's own leftmost columns first (the
  // literal top-left corner) - square-looking, sized off the row height
  // alone (see CoverArt::widthForHeight()), not the song's track count.
  // arrangement_grid_ sits immediately right of it, sized off the song's
  // own track count but capped, since its own internal scrolling handles
  // anything past that rather than this widget ever needing to be as wide
  // as the track list is long. chart_ gives up exactly that much width
  // (plus two divider columns) to make room; heatmap_/volume_meter_ are
  // untouched - a terminal too narrow for all five just clamps chart_ down
  // toward 1 column rather than a fuller "drop the least-used scope first"
  // rebalance, deferred for now.
  int cover_art_width = CoverArt::widthForHeight(kScopeHeight);
  int cover_art_divider_x = cover_art_width;
  cover_art_->resize(kScopeHeight, cover_art_width).move(scope_row, 0);

  auto num_tracks = static_cast<int>(getController().getSong().getPlayableTrackIds().size());
  // ArrangementGrid spends 2 columns per track (its own identifier cell
  // plus a shared padding cell - see ArrangementGrid.cpp's own
  // kColWidth), 1 for the leading padding cell before the first track
  // and 1 for its locator column, so its width is doubled plus two here
  // to actually fit the same track count this clamp implies.
  int matrix_width = std::clamp(num_tracks, 4, 24) * 2 + 2;
  int matrix_x = cover_art_divider_x + 1;
  int matrix_divider_x = matrix_x + matrix_width;
  arrangement_grid_->resize(kScopeHeight, matrix_width).move(scope_row, matrix_x);

  int chart_x = matrix_divider_x + 1;
  int chart_width = std::max(1, cols - chart_x - 9 - kHeatmapWidth - 2); // -2 for the single-column dividers on either side of the heatmap
  int divider1_x = chart_x + chart_width, divider2_x = divider1_x + 1 + kHeatmapWidth;
  // Live View places these in its own left column below.
  if (getView() == View::ARRANGEMENT) {
    chart_->setVisible(show_scopes);
    heatmap_->setVisible(show_scopes);
    chart_->resize(kScopeHeight, chart_width).move(scope_row, chart_x);
    heatmap_->resize(kScopeHeight, kHeatmapWidth).move(scope_row, divider1_x + 1);
  }
  volume_meter_->resize(kScopeHeight, 9).move(scope_row, divider2_x + 1);

  // Single-column dividers between the five scopes - drawn once here
  // rather than per-frame, since these columns fall outside every scope's
  // own resized rectangle, so nothing else ever repaints over them. (When
  // hidden, the workspace below covers these rows instead.)
  setFgColor(styles_.window_border_color);
  setBgColor(styles_.window_bg_color);
  const char * divider_glyph = styles_.grid_lines ? "│" : " ";
  for (int row = 0; show_scopes && row < kScopeHeight; row++) {
    putstr(scope_row + row, cover_art_divider_x, divider_glyph);
    putstr(scope_row + row, matrix_divider_x, divider_glyph);
    putstr(scope_row + row, divider1_x, divider_glyph);
    putstr(scope_row + row, divider2_x, divider_glyph);
  }
  // The workspace below the scope row. A widget the current view doesn't
  // show gets the same rect as a visible one instead of being shrunk to
  // nothing, and moveToTop() raises the visible ones above it.
  int workspace_row = show_scopes ? 1 + kScopeHeight : 1;
  int workspace_rows = std::max(2, rows - 2 - workspace_row);
  if (getView() == View::LIVE) {
    // Live View: the left column (outline panel, with the scopes under
    // it) takes the full workspace height; beside it the clip grid takes at
    // most half the workspace, the pattern editor the rest.
    laid_out_clip_grid_height_ = clip_grid_->preferredHeight();
    int strip_rows = std::min(laid_out_clip_grid_height_, workspace_rows / 2);
    int outline_cols = live_outline_cols;
    int outline_rows = live_scopes ? workspace_rows - live_scope_rows : workspace_rows;
    int right_cols = cols - outline_cols;
    clip_grid_->setVisible(true);
    outline_view_->setVisible(true);
    clip_grid_->resize(strip_rows, right_cols).move(workspace_row, outline_cols);
    outline_view_->resize(outline_rows, outline_cols > 0 ? outline_cols - 1 : cols).move(workspace_row, 0);
    if (outline_cols > 0) {
      // The column's last cell is a divider, drawn here on the plane
      // underneath (static, like the scope row's dividers); its top cell
      // carries the header row's backdrop across to the clip grid's.
      setFgColor(styles_.window_border_color);
      for (int row = 0; row < workspace_rows; row++) {
        setBgColor(row == 0 && styles_.grid_lines ? styles_.heading_bg_color : styles_.window_bg_color);
        putstr(workspace_row + row, outline_cols - 1, divider_glyph);
      }
    }
    chart_->setVisible(live_scopes);
    heatmap_->setVisible(live_scopes);
    if (live_scopes) {
      int scope_x_width = outline_cols - 1;
      int chart_row = workspace_row + outline_rows + 1;
      int heatmap_row = chart_row + kScopeHeight + 1;
      // A title bar above each scope, like the outline's heading; with
      // lines between the columns it ends in a half block over the divider.
      const std::pair<int, const char *> titles[] = { { chart_row - 1, "Spectrum" }, { heatmap_row - 1, "DirAC" } };
      for (auto & [ title_row, title ] : titles) {
        setFgColor(styles_.window_accent_fg_color);
        setBgColor(styles_.heading_bg_color);
        putstr(title_row, 0, std::string(static_cast<size_t>(scope_x_width), ' '));
        putstr(title_row, 1, title);
        if (styles_.grid_lines) {
          setFgColor(styles_.heading_bg_color);
          setBgColor(styles_.window_bg_color);
          putstr(title_row, scope_x_width, "▌");
        }
      }
      chart_->resize(kScopeHeight, scope_x_width).move(chart_row, 0);
      heatmap_->resize(kScopeHeight, scope_x_width).move(heatmap_row, 0);
    } else {
      // Hidden above; the size is only kept sensible for when they return.
      chart_->resize(kScopeHeight, std::max(1, cols / 2));
      heatmap_->resize(kScopeHeight, kHeatmapWidth);
    }
    pattern_editor_->resize(workspace_rows - strip_rows, right_cols).move(workspace_row + strip_rows, outline_cols);
    if (outline_cols > 0) outline_view_->moveToTop();
    clip_grid_->moveToTop();
    pattern_editor_->moveToTop();
  } else {
    // Hidden, not only covered: whatever the pattern editor leaves blank
    // for a moment (or a picture its meters left) must not show it.
    clip_grid_->setVisible(false);
    outline_view_->setVisible(false);
    clip_grid_->resize(workspace_rows, cols).move(workspace_row, 0);
    outline_view_->resize(workspace_rows, cols).move(workspace_row, 0);
    pattern_editor_->resize(workspace_rows, cols).move(workspace_row, 0);
    pattern_editor_->moveToTop();
  }
  info_line_->resize(1, cols).move(rows - 2, 0);
  // Inline in the info bar, right-aligned - a separate plane, created
  // after info_line_ (initializeWidgets()) so it z-orders above whatever
  // info_line_'s own text/padding puts under it (see this file's own
  // menu_->raiseToTop() comment for this codebase's "later-created sits
  // above" plane-stacking rule). If the info bar gets too full for this
  // later, this is the one line to change to move it into a dedicated
  // control bar row instead.
  auto octave_width = octave_control_->preferredWidth();
  octave_control_->resize(1, octave_width).move(rows - 2, std::max(0, cols - octave_width));
  status_line_->resize(1, cols - 1).move(rows - 1, 0);
  clearLastRow();
}

bool
TerminalUI::renderComponents(bool refresh) {
  // See force_next_render_'s own comment on TerminalUI.h - consumed here
  // (the one call site whose own return value actually reaches startUI()'s
  // render() gate), not at whichever earlier, input-handling call site
  // actually requested it.
  if (getView() == View::LIVE && syncLiveView()) force_next_render_ = true;
  refresh = refresh || force_next_render_;
  force_next_render_ = false;
  bool render = false;
  // Scopes coming back on screen start empty at once: a silent analysis sends
  // nothing more to replace what they held.
  if (scopes_stale_ && scopes_on_screen_) {
    blankScopes();
    render = true;
  }
  // A choice list scrolling or easing its cursor draws itself each frame.
  if (choice_dialog_.animating()) {
    layoutChoiceDialog();
    render = true;
  }
  auto active = active_element_.lock();
  auto & song = getController().getSong();
  // The shared/global track selection - Song's own, not any one widget's
  // cursor (PatternEditor's own root-track-list index doesn't necessarily
  // line up position-for-position with ArrangementGrid's own (color-
  // eligible-only) list, which is why this is passed as a real id below,
  // not a bare index).
  int selected_track_id = song.getCurrentTrackId();

  // "toggle-record-arm"'s own arm-something-new branch reads these two
  // (Controller::isClipGridFocused()/setClipGridCursor()) at the
  // moment it actually arms, which can happen from a Launchpad's own CC19
  // press just as easily as from here - dispatched straight to Controller,
  // bypassing this class entirely - so both need to already be correct by
  // then, not computed on demand only when this class itself handles the
  // keypress. Pushed here, once every frame, rather than only on an
  // explicit focus change: simplest way to guarantee "always current"
  // without a second, easy-to-miss update path for every place focus or
  // Live View's own cursor can change.
  bool clip_grid_focused = getView() == View::LIVE && active == clip_grid_;
  getController().setClipGridFocused(clip_grid_focused);
  getController().syncMonitoring();
  getController().tickNotePressure();
  if (clip_grid_focused) {
    auto track_ids = song.getPlayableTrackIds();
    auto track_index = clip_grid_->getCursorTrackIndex();
    if (track_index >= 0 && track_index < static_cast<int>(track_ids.size())) {
      getController().setClipGridCursor(track_ids[static_cast<size_t>(track_index)], clip_grid_->getCursorClipIndex());
    }
  }

  // Every frame, whichever view shows: a track whose clip stops stays
  // where playback left it.
  std::unordered_map<int, ScenePatternSource::Playhead> playheads;
  for (auto & [ track_id, playhead ] : getController().getClipPlayer().playheads()) {
    if (playhead.clip_index >= 0) playheads[track_id] = { playhead.clip_index, playhead.row, playhead.elapsed, playhead.looping };
  }
  pattern_editor_->setLivePlayheads(std::move(playheads));

  // Only what the current view shows (see layout()) - a hidden widget
  // drawing would paint over whichever visible one shares its rect.
  if (getView() == View::LIVE) {
    clip_grid_->setClipStateSource([this](int track_id, int clip_index) {
      return getController().getClipPlayer().clipHighlight(track_id, clip_index);
    });
    clip_grid_->setTrackClipSource([this](int track_id) { return pattern_editor_->getLiveScene(track_id); });
    if (clip_grid_->render(styles_, refresh, active == clip_grid_)) { render = true; statCount("render:clip_grid_"); }
    if (isOutlineVisible()) if (outline_view_->render(styles_, refresh, active == outline_view_)) { render = true; statCount("render:outline_view_"); }
  }
  if (pattern_editor_->render(styles_, refresh, active == pattern_editor_)) { render = true; statCount("render:pattern_editor_"); }
  if (arrangement_grid_->render(styles_, refresh, active == arrangement_grid_, selected_track_id)) { render = true; statCount("render:arrangement_grid_"); }
  if (cover_art_->render(styles_, refresh)) { render = true; statCount("render:cover_art_"); }
  if (info_line_->render(styles_, refresh)) { render = true; statCount("render:info_line_"); }
  if (octave_control_->render(styles_, refresh)) { render = true; statCount("render:octave_control_"); }

  auto & clip_player = getController().getClipPlayer();
  clip_player.setAssignRow(arrangement_grid_->getCursorRow(song));
  clip_player.tick();

  if (launchpad_manager_) {
    auto track_ids = song.getPlayableTrackIds();
    // Populated every call regardless of which UI element actually has
    // focus - a Launchpad's own Live View (CC95/96, per-device) is
    // independent of that now, so this always needs to be ready with
    // wherever a press would actually land (see LaunchpadManager::
    // LiveWindow's own comment).
    LaunchpadManager::LiveWindow live;
    live.track_ids = arrangement_grid_->getVisibleTrackIds(song);
    launchpad_manager_->refresh(song, track_ids, getController().getPlaybackInfo(),
      track_ids.empty() ? -1 : indexOfTrack(track_ids, song.getCurrentTrackId()), getController(), live);
  }

  return render;
}

bool
TerminalUI::syncLiveView() {
  auto & song = getController().getSong();

  // The track: the shared current track, in each widget's own index space.
  int track_id = song.getCurrentTrackId();
  if (track_id != synced_track_id_) {
    auto playable = song.getPlayableTrackIds();
    if (std::find(playable.begin(), playable.end(), track_id) != playable.end()) clip_grid_->setCursorTrackIndex(indexOfTrack(playable, track_id));
    auto roots = pattern_editor_->trackIds(song);
    auto it = std::find(roots.begin(), roots.end(), track_id);
    if (it != roots.end() && pattern_editor_->getCursorTrackIndex() != it - roots.begin()) pattern_editor_->setCursorTrack(static_cast<int>(it - roots.begin()));
    synced_track_id_ = track_id;
  }

  // No clip row is shared: the clip grid's cursor is its own, and never
  // moves a track's position (only the pattern editor, or a launched clip
  // starting to play, does) - the cursor track's clip being edited is
  // marked instead (setTrackClipSource()).

  if (clip_grid_->preferredHeight() == laid_out_clip_grid_height_) return false;
  layout();
  return true;
}

std::vector<std::shared_ptr<cellui::Element>>
TerminalUI::focusableElements() const {
  if (getView() == View::ARRANGEMENT) {
    if (!scopes_visible_) return { pattern_editor_ };
    return { pattern_editor_, arrangement_grid_ };
  }
  // Screen order, the way Emacs cycles windows: the outline panel
  // (top-left), the clip grid beside it, then the pattern editor below.
  std::vector<std::shared_ptr<cellui::Element>> elements;
  if (isOutlineVisible()) elements.push_back(outline_view_);
  elements.push_back(clip_grid_);
  elements.push_back(pattern_editor_);
  return elements;
}

void
TerminalUI::viewChanged() {
  auto active = active_element_.lock();
  auto elements = focusableElements();
  if (std::find(elements.begin(), elements.end(), active) == elements.end()) {
    // Focus was on a widget this view no longer shows - land on the view's
    // own main widget.
    if (getView() == View::LIVE) {
      auto playable = getController().getSong().getPlayableTrackIds();
      clip_grid_->setCursorTrackIndex(indexOfTrack(playable, getController().getSong().getCurrentTrackId()));
      active_element_ = clip_grid_;
    } else {
      active_element_ = pattern_editor_;
    }
  }
  // An inline editor left open on a widget that's no longer shown would
  // keep swallowing keys.
  if (getView() == View::ARRANGEMENT) clip_grid_->cancelReaderEdit();
  if (getView() != View::LIVE || !isOutlineVisible()) outline_view_->closeInfoPopup();
  outline_view_->setShown(getView() == View::LIVE && isOutlineVisible());
  // Live View edits clips one scene at a time; Arrangement view edits
  // the arrangement and placed clips.
  pattern_editor_->cancelReaderEdit();
  pattern_editor_->setLiveMode(getView() == View::LIVE);
  // Entering Live View, the clip grid and the pattern editor start on
  // the same track (syncLiveView() keeps them together from here).
  synced_track_id_ = -1;
  layout();
  // Not a direct renderComponents(true) call here - see
  // force_next_render_'s own comment on TerminalUI.h.
  force_next_render_ = true;
}

bool
TerminalUI::tryActivate(int y, int x, std::shared_ptr<cellui::Element> element) {
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
TerminalUI::offerInput(const cellui::Input & input) {
  bool handled = false;

  // No open reader (a StatusLine prompt, any widget's inline
  // editor - see PatternEditor::isReaderActive()'s own comment) may let a
  // global keybinding (Space/toggle-playing, C-x C-c/quit, ...) steal a
  // keystroke meant for it.
  bool reader_active = status_line_->isReaderActive() || pattern_editor_->isReaderActive() ||
    clip_grid_->isReaderActive();
  // The choice dialog is modal too, and owns its keys until it is chosen
  // from or cancelled.
  if (command_palette_.isOpen() && input.getId() != Key::Resize && !(input.hasCtrl() && input.getId() == 'l')) {
    handleCommandPaletteInput(input);
    return true;
  }
  if (choice_dialog_.isOpen() && input.getId() != Key::Resize && !(input.hasCtrl() && input.getId() == 'l')) {
    handleChoiceDialogInput(input);
    return true;
  }
  // The info dialog is modal: it closes on its own keys and swallows
  // everything else but a resize or redraw. The mouse is the terminal's
  // while it is open (raiseOverlays()), so its text can be selected.
  if (info_dialog_.isOpen() && input.getId() != Key::Resize && !(input.hasCtrl() && input.getId() == 'l')) {
    auto id = input.getId();
    // Not a release: the Enter that opened it, from the command palette, is
    // released after it is open.
    if (input.getKind() != InputEvent::Kind::RELEASE &&
        (id == Key::Escape || id == Key::Enter || id == 'q' || (input.hasCtrl() && id == 'g'))) {
      closeInfoDialog();
    }
    return true;
  }
  // A modal popup takes everything but a resize or redraw.
  if (outline_view_ && outline_view_->isModal() && input.getId() != Key::Resize && !(input.hasCtrl() && input.getId() == 'l')) {
    outline_view_->offerInput(input);
    return true;
  }
  // An open menu owns the mouse: its dropdown is drawn over the widgets, and
  // a click on an item must not also reach the widget underneath it (which
  // would move its cursor, and so change what the item's command acts on).
  if (menu_->isOpen() && input.getId() >= Key::Motion && input.getId() <= Key::Button11) {
    if (menu_->offerInput(input)) {
      if (auto cmd = menu_->takeActivatedCommand(); !cmd.empty()) {
	if (!getController().sendCommand(cmd)) setStatus("Invalid command");
      }
    }
    return true;
  }
  if (octave_control_->isEditing() && octave_control_->offerInput(input)) return true;
  // A command (undo, save, ...) must see a half-typed velocity or delay.
  if (input.getKind() != InputEvent::Kind::RELEASE && (input.hasCtrl() || input.hasAlt())) pattern_editor_->commitPendingValue();
  if (!reader_active && dispatchCommand(input)) return true;

  if (input.getId() == Key::Resize) {
    // The screen and the root plane already have the new size
    // (resizeScreen(), as the event was read); the terminal reflowed, so
    // the next frame redraws everything.
    refresh();
    layout();
    layoutInfoDialog();
    layoutChoiceDialog();
    layoutCommandPalette();
    // Deferred, not a direct renderComponents(true) call - see
    // force_next_render_'s own comment on TerminalUI.h: this runs from
    // inside input handling, before startUI()'s own main loop reaches its
    // own renderComponents() call (the one that actually gates
    // render(), the real terminal update) - a direct call here would
    // draw everything correctly into the planes, then
    // report "nothing left to redraw" right back to that outer call,
    // which would then skip the flush and never actually show it.
    force_next_render_ = true;
  } else if (input.hasCtrl() && input.getId() == 'l') {
    refresh();
  } else if (input.getId() == Key::Button4 || input.getId() == Key::Button5) {
    // The mouse wheel scrolls whichever shown widget is under the mouse,
    // without moving focus (Emacs's own mouse-wheel-follow-mouse default) -
    // each widget scrolls its view, never its cursor.
    for (auto & element : focusableElements()) {
      auto [pos_y, pos_x] = element->getPosition();
      auto [rows, cols] = element->getDim();
      if (input.getY() >= pos_y && input.getY() < pos_y + rows && input.getX() >= pos_x && input.getX() < pos_x + cols) {
	element->offerInput(input);
	break;
      }
    }
    return true;
  } else if (input.getId() == Key::Button1 && (octave_control_->arrowPressPending() ||
                                                (input.getKind() != InputEvent::Kind::RELEASE && octave_control_->hitsArrow(input.getY(), input.getX())))) {
    // The octave arrows act on click without taking focus from the active element.
    octave_control_->offerInput(input);
    return true;
  } else if (input.getId() == Key::Button1 && (input.getKind() == InputEvent::Kind::RELEASE || clip_grid_->isMouseDown() || pattern_editor_->isMouseDown())) {
    // A release, or a press repeating as a drag moves, belongs to the
    // widget the button went down on - it must not hand focus to whatever
    // the pointer is over now. The release also ends the press everywhere,
    // wherever it lands.
    if (input.getKind() == InputEvent::Kind::RELEASE) {
      clip_grid_->releaseMouse();
      pattern_editor_->releaseMouse();
    }
  } else if (input.getId() == Key::Button1) {
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
    // Only the widgets the current view shows - a hidden one can share a
    // visible one's rect (see layout()), and a click must activate
    // whichever is visible.
    bool activated = false;
    for (auto & element : focusableElements()) {
      if (!activated) activated = tryActivate(input.getY(), input.getX(), element);
    }
    activated = tryActivate(input.getY(), input.getX(), octave_control_) || activated;

    // Fall back to the pattern editor (in every view) if the click landed
    // somewhere no widget claims (e.g.
    // the FFT/heatmap/loudness scope strip, or the dividers between
    // them). Without this, active_element_ was left permanently empty (a
    // real, confirmed bug): every subsequent keyboard command routed
    // through it (offerInput()'s active_element_ fallback below, and
    // Launchpad button commands via executeCommand()) silently no-op'd -
    // including plain Up/Down arrow - until the user happened to click
    // directly back on the workspace.
    // A click on the menu bar (the top row) leaves focus where it was.
    bool on_menu = input.getY() == 0;
    if (!activated) active_element_ = (on_menu && previous_active_element) ? previous_active_element : pattern_editor_;

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
      // setCommandFallback(), initializeWidgets()); calling executeCommand()
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
TerminalUI::setStatus(std::string s) {
  if (status_line_) {
    status_line_->setMessage(std::move(s));
    render();
  }
}

void
TerminalUI::showInfoDialog(const std::string & title, const std::string & markdown) {
  info_dialog_title_ = title;
  info_dialog_markdown_ = markdown;
  layoutInfoDialog();
  force_next_render_ = true;
}

void TerminalUI::showChoiceDialog(const std::string & title, std::vector<Choice> choices, int current,
                                  std::function<void(int)> on_choose) {
  if (choices.empty()) {
    setStatus(title + ": nothing to choose from");
    return;
  }
  std::vector<std::string> labels;
  for (auto & choice : choices) labels.push_back(choice.label);
  choice_callback_ = std::move(on_choose);
  choice_dialog_.open(title, std::move(labels), current);
  layoutChoiceDialog();
  force_next_render_ = true;
}

// How the active widgets bind `command` on this display, the focused one first.
std::string TerminalUI::bindingFor(const std::string & command) const {
  if (auto active = active_element_.lock()) {
    if (auto binding = active->bindingFor(command); !binding.empty()) return binding;
  }
  if (auto binding = pattern_editor_->bindingFor(command); !binding.empty()) return binding;
  if (auto binding = clip_grid_->bindingFor(command); !binding.empty()) return binding;
  return cellui::Element::bindingFor(command);
}

void TerminalUI::showCommandPalette() {
  // Opening it aborts a widget's own half-typed edit, like any prompt.
  pattern_editor_->cancelReaderEdit();
  clip_grid_->cancelReaderEdit();
  std::vector<cellui::PaletteEntry> entries;
  for (auto & name : commandCompletions("")) {
    entries.push_back({name, commandDescription(name), bindingFor(name)});
  }
  command_palette_.open(std::move(entries));
  layoutCommandPalette();
  force_next_render_ = true;
}

void TerminalUI::layoutCommandPalette() {
  if (!command_palette_.isOpen()) return;
  auto [screen_rows, screen_cols] = getDim();
  command_palette_.show(getPlane(), screen_rows, screen_cols);
}

void TerminalUI::handleCommandPaletteInput(const cellui::Input & input) {
  auto result = command_palette_.offerInput(input);
  if (result == cellui::CommandPalette::Result::NONE) {
    layoutCommandPalette(); // redraw with the new query or selection
    force_next_render_ = true;
    return;
  }
  std::string name;
  if (result == cellui::CommandPalette::Result::CHOSEN && command_palette_.chosen()) name = command_palette_.chosen()->name;
  // Closed before the command runs: it may open a prompt or a dialog of its
  // own, and nothing should be left modal while it does.
  command_palette_.close();
  force_next_render_ = true;
  if (!name.empty() && !getController().sendCommand(name)) setStatus("Invalid command");
}

void TerminalUI::layoutModalScrim() {
  if (!choice_dialog_.isOpen() && info_dialog_title_.empty()) {
    modal_scrim_.close();
    return;
  }
  auto [screen_rows, screen_cols] = getDim();
  modal_scrim_.show(getPlane(), screen_rows, screen_cols, getPlane().getStyles().theme.modal_backdrop);
}

void TerminalUI::layoutChoiceDialog() {
  if (!choice_dialog_.isOpen()) return;
  layoutModalScrim();
  auto [screen_rows, screen_cols] = getDim();
  choice_dialog_.show(getPlane(), screen_rows, screen_cols);
}

void TerminalUI::handleChoiceDialogInput(const cellui::Input & input) {
  auto result = choice_dialog_.offerInput(input);
  if (result == ChoiceDialog::Result::NONE) {
    layoutChoiceDialog(); // redraw with the moved selection
    force_next_render_ = true;
    return;
  }
  auto index = choice_dialog_.selected();
  // Closed before the callback runs: it may report on the status line, and
  // nothing should be left modal while it does.
  auto callback = std::move(choice_callback_);
  closeChoiceDialog();
  if (result == ChoiceDialog::Result::CHOSEN && callback) callback(index);
}

// Centers the dialog on the screen.
void
TerminalUI::layoutInfoDialog() {
  if (info_dialog_title_.empty()) return;
  layoutModalScrim();
  auto [screen_rows, screen_cols] = getDim();
  auto width = std::min(60, screen_cols);
  info_dialog_.show(getPlane(), info_dialog_title_, info_dialog_markdown_, width, screen_rows);
  info_dialog_.center(screen_rows, screen_cols);
}

void
TerminalUI::handlePlaybackEvent(PlaybackEvent & ev) {
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

  // Each caller's own note-recording clips (Controller::
  // ensureNoteRecordingClip()) grow independently - see
  // extendRecordingClipsIfNeeded()'s own comment on why this needs no
  // gate. Each call's own held-track-ids argument scopes growth to a
  // track with a note actually held right now.
  if (pattern_editor_) getController().extendRecordingClipsIfNeeded(pattern_editor_->getAutoRecordClipIds(), pattern_editor_->getActiveNoteTrackIds());
  if (launchpad_manager_) getController().extendRecordingClipsIfNeeded(launchpad_manager_->getAutoRecordClipIds(), launchpad_manager_->getActiveNoteTrackIds());
  // A live mic take's own counterpart to the two calls above - see
  // extendRecordingSampleClipIfNeeded()'s own comment for why it needs no
  // clip_ids/held_track_ids of its own.
  getController().extendRecordingSampleClipIfNeeded();

  // Redraw only for a snapshot that shows something new: a block of idle
  // audio repeats the last one.
  auto signature = ev.getInfo().signature();
  if (g_ui_stats_on && signature != last_playback_signature_) {
    // Which part of the snapshot differs, for SYNTH_UI_STATS.
    auto parts = ev.getInfo().signatureParts();
    for (size_t i = 0; i < parts.size() && i < last_playback_parts_.size(); i++) {
      if (parts[i].second != last_playback_parts_[i]) statCount(std::string("changed:") + parts[i].first);
    }
    last_playback_parts_.clear();
    for (auto & part : parts) last_playback_parts_.push_back(part.second);
  }
  if (signature != last_playback_signature_) {
    last_playback_signature_ = signature;
    ev.redraw();
  }
}

// Shows the scopes empty and forgets what they held, so the next result
// redraws them whatever it equals.
void
TerminalUI::blankScopes() {
  scopes_stale_ = false;
  chart_->clear();
  heatmap_->setGrid(std::vector<float>(DiracAnalyzer::kGridSize, 0.0f), std::vector<float>(DiracAnalyzer::kGridSize, 0.0f));
  heatmap_->commit();
  volume_meter_->clear(last_meter_label_);
  last_fft_.clear();
  last_brightness_.clear();
  last_saturation_.clear();
  last_loudness_.clear();
  dirac_running_max_ = 0.0f;
}

void
TerminalUI::handleVisualizationResultEvent(VisualizationResultEvent & ev) {
  // If a newer event is already queued behind this one, this one's visual
  // result is about to be immediately overwritten - skip the
  // comparatively expensive chart/meter update work for it. Doesn't
  // change what eventually gets rendered (the last event in a batch
  // always wins anyway, via plain overwrite); it only avoids redoing that
  // work once per superseded event during a catch-up burst, so the app
  // catches up faster instead of falling further behind.
  bool superseded = getController().getUIEventQueue().hasEvents();
  bool changed = superseded; // a superseded event is redrawn by the one behind it
  // Scopes that are off screen aren't updated at all.
  if (!superseded && scopes_on_screen_) {
    if (scopes_stale_) {
      blankScopes();
      changed = true;
    }
    last_meter_label_ = ev.getMeterLabel();
    // Raw, pre-mixdown per-channel levels (ambisonic bus, then always
    // AuxA/AuxB last - see VisualizationThread.cpp) rather than the final
    // decoded L/R output.
    // A reading equal to the last still has to go in while the bars fall.
    if (getView() == View::ARRANGEMENT && (ev.getChannelLoudness() != last_loudness_ || !volume_meter_->atRest())) {
      last_loudness_ = ev.getChannelLoudness();
      volume_meter_->setLevels(ev.getChannelLoudness(), ev.getMeterLabel());
      changed = true;
    }

    if (!ev.getFFT().empty() && ev.getFFT() != last_fft_) {
      last_fft_ = ev.getFFT();
      if (chart_->setSpectrum(ev.getFFT(), ev.getFFTBinHz())) changed = true;
    }

    if (ev.hasDiracGrid()) {
      // displayed[cell] = the grid's own
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
      // rate) so a quiet passage
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
      // Compared at 1/512: a smaller step is no change in any pixel's colour.
      auto quantize = [](const std::vector<float> & v) { std::vector<int> q(v.size()); for (size_t i = 0; i < v.size(); i++) q[i] = static_cast<int>(v[i] * 512.0f + 0.5f); return q; };
      auto quantized_brightness = quantize(brightness), quantized_saturation = quantize(saturation);
      if (quantized_brightness != last_brightness_ || quantized_saturation != last_saturation_) {
	last_brightness_ = std::move(quantized_brightness);
	last_saturation_ = std::move(quantized_saturation);
	heatmap_->setGrid(brightness, saturation);
	heatmap_->commit();
	changed = true;
      }
    }
  }

  if (changed) ev.redraw();
}

void
TerminalUI::handleLogEvent(LogEvent & ev) {
  setStatus(ev.getText());
}

void
TerminalUI::handleMidiEvent(MidiEvent & ev) {
  pattern_editor_->handleMidiEvent(ev);
}

int
TerminalUI::launchpadEditStepSize() const {
  return pattern_editor_->getEditStepSize();
}

void
TerminalUI::wireLaunchpad(LaunchpadManager & launchpad_manager) {
  // launchpad_manager_ itself is already set by UI::start() before this
  // hook runs.
  // "move-row-up"/"move-row-down" while in GridMode::LIVE move
  // ArrangementGrid's own bar cursor instead of scrolling a pad-grid row
  // window - see LaunchpadManager::live_move_bar_callback_'s own
  // comment for why.
  launchpad_manager.setLiveMoveBarCallback([this](int delta) { arrangement_grid_->moveCursorBar(delta); });
  // "pad-next-track"/"pad-prev-track" outside GridMode::LIVE move the one
  // shared cursor every connected Launchpad follows - see
  // LaunchpadManager::track_move_callback_'s own comment for why. Also
  // moves ClipGrid's own cursor, kept in step the same way it already
  // seeds from PatternEditor's cursor when Live View first opens
  // (setCursorTrackIndex()'s own comment) - a track change made on the
  // Launchpad has to be reflected there too, not just in PatternEditor,
  // regardless of which one currently has terminal focus. backend_ids
  // (the same track_ids getPlayableTrackIds() column order is keyed by)
  // isn't available here, but PatternEditor's own cursor is already an
  // index into that identical ordering, so no translation is needed.
  launchpad_manager.setTrackMoveCallback([this](int new_track_index) {
    pattern_editor_->setCursorTrack(new_track_index);
    clip_grid_->setCursorTrackIndex(new_track_index);
  });
  // ClipGrid's own Enter key - acts exactly like a Launchpad Live
  // view pad press on the same cell (ClipPlayer::triggerClip(), same as
  // handleLivePadEvent() itself resolves to).
  clip_grid_->setTriggerCallback([this](int track_id, int clip_index) {
    getController().getClipPlayer().triggerClip(track_id, clip_index);
  });
  clip_grid_->setSceneCallback([this](int clip_index) {
    getController().getClipPlayer().launchScene(clip_index, getController().getSong().getPlayableTrackIds());
  });
  clip_grid_->setStopAllCallback([this]() { getController().getClipPlayer().stopAllTracks(); });
  // A launch that writes into the arrangement starts the transport the
  // way a Launchpad recording does, so disarming stops it again.
  getController().getClipPlayer().setAssignPlaybackStarter([this]() { launchpad_manager_->startAssignPlayback(getController()); });
  // Opening a clip for step editing on a Launchpad
  // (Controller::toggleDrumClipFocus()) - focusing it (Controller::setFocusedClip()) moves
  // the shared track cursor to it (so PatternEditor's/the Launchpad's own
  // fallback_track_index-following resolve there next), switches the
  // Launchpad that opened it to the step grid and starts it from a
  // known page and octave (LaunchpadManager::openStepView()); closing one
  // (a second press on the clip already open -
  // Controller::clearFocusedClip()) hands that device back to Live View
  // instead, rather than leaving it stuck showing a step grid with nothing
  // left focused to edit there.
  getController().setClipSelectListener([this](int track_id, int clip_index) {
    auto & song = getController().getSong();
    auto root_ids = pattern_editor_->trackIds(song);
    auto root = std::find(root_ids.begin(), root_ids.end(), track_id);
    if (root != root_ids.end()) pattern_editor_->setCursorTrack(static_cast<int>(root - root_ids.begin()));
    auto playable = song.getPlayableTrackIds();
    auto column = std::find(playable.begin(), playable.end(), track_id);
    if (column != playable.end()) clip_grid_->setCursorTrackIndex(static_cast<int>(column - playable.begin()));
    clip_grid_->setCursorClipIndex(clip_index);
  });
  getController().setUndoCellListener([this](const Song::EditPlace & place) { pattern_editor_->focusCell(place); });
  getController().setUndoFocusListener([this](int track_id, int clip_index, int row) {
    auto & song = getController().getSong();
    auto root_ids = pattern_editor_->trackIds(song);
    auto root = std::find(root_ids.begin(), root_ids.end(), track_id);
    if (root != root_ids.end()) pattern_editor_->setCursorTrack(static_cast<int>(root - root_ids.begin()));
    auto playable = song.getPlayableTrackIds();
    auto column = std::find(playable.begin(), playable.end(), track_id);
    if (column != playable.end()) clip_grid_->setCursorTrackIndex(static_cast<int>(column - playable.begin()));
    clip_grid_->setCursorClipIndex(clip_index);
    pattern_editor_->focusLiveRow(track_id, clip_index, row);
  });
  getController().setDrumEditRequestListener([this](int track_id, bool opened) {
    if (opened) {
      auto & song = getController().getSong();
      auto track_ids = pattern_editor_->trackIds(song);
      auto it = std::find(track_ids.begin(), track_ids.end(), track_id);
      if (it != track_ids.end()) pattern_editor_->setCursorTrack(static_cast<int>(it - track_ids.begin()));
      launchpad_manager_->openStepView();
      // Meant to be heard in isolation - stops the transport if it
      // happens to be running (the focused-clip audition below only ever
      // engages while stopped anyway - LaunchpadManager::refresh()'s own
      // audition_active) and releases whatever any other track's own
      // Live-View-triggered clip was still sounding, rather than
      // layering the drum edit preview under either one.
      if (getController().getPlaybackInfo().isPlaying()) getController().togglePlaying();
      getController().getClipPlayer().silenceAll();
    } else {
      launchpad_manager_->closeStepView();
    }
  });
}

void
TerminalUI::startUI(AudioAPI &, MidiInput & midi, LaunchpadIO & launchpad_io) {
  int out_pipe[2];

  if (pipe(out_pipe) != 0) { // make a pipe
    exit(1);
  }

  // stderr always gets redirected into the pipe below (so it can be shown
  // on the status line) - but if the caller already redirected it
  // themselves (2> some.log, or piped into another process), that
  // destination is the one place a person can actually go read a full,
  // unbounded, post-mortem log after the fact; the status line only ever
  // shows the latest single line, and only while the UI is still up. Save
  // a duplicate of it here, before it's overwritten, so every future write
  // to fd 2 can still reach it too (see the out_pipe-draining branch
  // below) - a real terminal (isatty true, ordinary interactive use, no
  // redirection) has no such separate destination to preserve, so this
  // stays -1 and nothing is duplicated.
  int real_stderr_fd = isatty(STDERR_FILENO) ? -1 : dup(STDERR_FILENO);

  dup2(out_pipe[1], STDERR_FILENO); // redirect stderr to the pipe
  close(out_pipe[1]);

  // In the order the loop handles them.
  WaitSet waits;
  auto input_group = waits.group();
  auto events_group = waits.group();
  auto stderr_group = waits.group();
  auto midi_group = waits.group();
  auto launchpad_group = waits.group();

  waits.addFd(input_group, backend_.inputFd());
  waits.addFd(input_group, backend_.wakeFd());
  waits.add(events_group, getController().getUIEventQueue().notifier());
  waits.addFd(stderr_group, out_pipe[0]);
  midi.addTo(waits, midi_group);
  launchpad_io.addTo(waits, launchpad_group);


  renderComponents(true);

  string waiting_stderr;
  // Playback and visualization events arrive with every audio block (about
  // 190 a second); drawing for each of them would use far more of the
  // terminal than the eye can follow, so a frame is drawn at most every
  // kSteadyFrameInterval and the changes in between are folded into it.
  constexpr auto kSteadyFrameInterval = std::chrono::milliseconds(25);
  // A view that scrolls smoothly is shown at the display's pace, not the steady one.
  constexpr auto kAnimatedFrameInterval = std::chrono::milliseconds(16);
  auto animating = [&]() { return (arrangement_grid_ && arrangement_grid_->animating()) || (outline_view_ && outline_view_->animating()) || choice_dialog_.animating(); };
  auto min_frame_interval = [&]() { return animating() ? kAnimatedFrameInterval : kSteadyFrameInterval; };
  auto last_frame = std::chrono::steady_clock::now() - kSteadyFrameInterval;
  bool frame_pending = false;
  bool render_pending = false; // a redraw asked for while the frame was held back
  // Between events that ask for nothing, frames come this seldom: the widgets
  // have nothing new to find, and the audio thread wakes the loop ~190 times
  // a second even when paused.
  constexpr auto kIdleFrameInterval = std::chrono::milliseconds(100);

  // SYNTH_UI_STATS=<file>: once a second, what woke the UI thread and what it
  // drew, to find where idle CPU goes.
  const char * stats_path = getenv("SYNTH_UI_STATS");
  g_ui_stats_on = stats_path != nullptr;
  auto stats_since = std::chrono::steady_clock::now();
  timespec stats_cpu_since {};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stats_cpu_since);
  auto stats_thread_ticks = stats_path ? threadCpuTicks() : std::map<std::string, long>{};
  auto count = [](const std::string & what) { statCount(what); };

  // A turn: what the handlers below set, then the frame logic in the after
  // hook. The loop is the backend's own.
  bool render = false;
  bool other_wake = false; // input, MIDI, Launchpad or stderr woke the loop, not just the event queue
  auto & loop = backend_.eventLoop();
  auto add_source = [&](WaitSet::Group group, cellui::EventLoop::Handler handler) {
    loop.addSource(waits.fds(group), std::move(handler));
  };
  add_source(input_group, [&] {
    count("fd" + std::to_string(input_group));
    other_wake = true;
    render |= readInput();
  });
  add_source(events_group, [&] {
    count("fd" + std::to_string(events_group));
    auto event = getController().getUIEventQueue().pop();
    auto t0 = std::chrono::steady_clock::now();
    handleEvent(*event);
    if (stats_path) g_ui_stats["us:" + std::string(typeid(*event).name())] += static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    if (stats_path) {
      count(typeid(*event).name());
      if (event->needRedraw()) count("redraw:" + std::string(typeid(*event).name()));
    }
    if (event->needRedraw()) render = true;
    while (getController().getUIEventQueue().hasEvents()) {
      auto next_event = getController().getUIEventQueue().pop();
      auto t1 = std::chrono::steady_clock::now();
      handleEvent(*next_event);
      if (stats_path) g_ui_stats["us:" + std::string(typeid(*next_event).name())] += static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1).count());
      if (stats_path) {
        count(typeid(*next_event).name());
        if (next_event->needRedraw()) count("redraw:" + std::string(typeid(*next_event).name()));
      }
      if (next_event->needRedraw()) render = true;
    }
  });
  add_source(stderr_group, [&] {
    count("fd" + std::to_string(stderr_group));
    other_wake = true;
    char buffer[4096];
    int r = read(out_pipe[0], buffer, 4096);
    if (real_stderr_fd >= 0 && r > 0) {
      // Raw bytes, not the line-split/status-line text below - this
      // is a faithful mirror of what stderr would have received
      // without the redirect above, not a reformatted copy. Loops to
      // cover a partial write (a real possibility for a pipe/socket
      // destination); best-effort otherwise - a write() failure here
      // (e.g. the destination process on the other end of a pipe
      // already exited) must never take the whole UI down with it.
      int written = 0;
      while (written < r) {
        auto n = write(real_stderr_fd, buffer + written, static_cast<size_t>(r - written));
        if (n <= 0) break;
        written += static_cast<int>(n);
      }
    }
    waiting_stderr += string(buffer, static_cast<size_t>(r));
    while (1) {
      auto pos = waiting_stderr.find('\n');
      if (pos != string::npos) {
        setStatus(waiting_stderr.substr(0, pos));
        waiting_stderr.erase(0, pos + 1);
      } else {
        break;
      }
    }
  });
  add_source(midi_group, [&] {
    count("fd" + std::to_string(midi_group));
    other_wake = true;
    auto evs = midi.readEvents();
    // setStatus("got midi events: " + to_string(evs.size()));

    for (auto & ev : evs) {
      handleEvent(ev);
      if (ev.needRedraw()) render = true;
    }
  });
  add_source(launchpad_group, [&] {
    count("fd" + std::to_string(launchpad_group));
    other_wake = true;
    auto evs = launchpad_io.pollEvents();

    for (auto & ev : evs) {
      handleEvent(*ev);
      if (ev->needRedraw()) render = true;
    }
  });

  loop.setBefore([&]() -> int {
    render = false;
    other_wake = false;
    if (stats_path && std::chrono::steady_clock::now() - stats_since >= std::chrono::seconds(1)) {
      timespec cpu_now{};
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_now);
      g_ui_stats["cpu_us"] = static_cast<int>((cpu_now.tv_sec - stats_cpu_since.tv_sec) * 1000000L + (cpu_now.tv_nsec - stats_cpu_since.tv_nsec) / 1000L);
      stats_cpu_since = cpu_now;
      // Every thread's share of a CPU over the second, in percent.
      auto thread_ticks = threadCpuTicks();
      for (auto & [name, t] : thread_ticks) {
        auto it = stats_thread_ticks.find(name);
        if (it != stats_thread_ticks.end()) g_ui_stats["cpu%:" + name] = static_cast<int>(100 * (t - it->second) / std::max(1L, sysconf(_SC_CLK_TCK)));
      }
      stats_thread_ticks = std::move(thread_ticks);
      if (FILE * f = fopen(stats_path, "a")) {
        for (auto & [what, n] : g_ui_stats) fprintf(f, "%s=%d ", what.c_str(), n);
        fputs("\n", f);
        fclose(f);
      }
      g_ui_stats.clear();
      stats_since = std::chrono::steady_clock::now();
    }

    updateEscapeIndicator();

    int poll_timeout_ms = escapeIndicatorPollTimeoutMs();
    // A view still moving toward where it was scrolled to asks for the next frame.
    if (animating()) frame_pending = true;
    // A redraw deferred by the frame limiter below wakes the loop when it is due.
    if (frame_pending) {
      auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(min_frame_interval() - (std::chrono::steady_clock::now() - last_frame)).count() + 1;
      wait = std::max<long>(wait, 1);
      poll_timeout_ms = poll_timeout_ms < 0 ? static_cast<int>(wait) : std::min(poll_timeout_ms, static_cast<int>(wait));
    }

    // The terminal's input waits a few milliseconds for the rest of a
    // sequence a read split; when that runs out, what it holds is settled.
    if (auto deadline = backend_.nextDeadline()) {
      auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now()).count() + 1;
      wait = std::max<long>(wait, 0);
      poll_timeout_ms = poll_timeout_ms < 0 ? static_cast<int>(wait) : std::min(poll_timeout_ms, static_cast<int>(wait));
    }
    return poll_timeout_ms;
  });

  loop.setAfter([&](bool woken) {
    if (shouldClose()) {
      loop.quit();
      return;
    }
    if (!woken && backend_.nextDeadline() && std::chrono::steady_clock::now() >= *backend_.nextDeadline()) {
      readInput();
      other_wake = true;
      woken = true;
    }
    count(woken ? "wake" : "timeout");

    if (woken || frame_pending) {
      render_pending |= render;
      auto now = std::chrono::steady_clock::now();
      if (!other_wake && !render_pending && !frame_pending && now - last_frame < kIdleFrameInterval) {
	count("idle-wake-skipped");
        return;
      }
      if (now - last_frame < min_frame_interval()) {
	frame_pending = true;
        return;
      }
      frame_pending = false;
      last_frame = now;
      render = render_pending;
      render_pending = false;

      auto t2 = std::chrono::steady_clock::now();
      bool components = renderComponents();
      if (stats_path) g_ui_stats["us:renderComponents"] += static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2).count());
      count("frame");
      if (components) count("frame:components-asked-redraw");
      render |= components;

      if (render) {
        count("present");
        TerminalUI::render();
      }
      // A colour still easing needs the next frame even if nothing else wakes
      // the loop.
      if ((getView() == View::LIVE && (clip_grid_->isAnimating() || outline_view_->isAnimating())) || pattern_editor_->isAnimating() ||
          arrangement_grid_->isAnimating()) frame_pending = true;
    }
  });

  if (!shouldClose()) loop.run();
}
