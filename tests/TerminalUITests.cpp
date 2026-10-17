#include "TestFramework.h"

#include "TerminalUIFixture.h"

#include <filesystem>
#include <fstream>

using namespace terminal_ui_test;


TEST(terminal_ui_presents_a_frame_with_its_focus_and_planes) {
  UIFixture f;
  CHECK(f.terminal.frameCount() >= 1);
  CHECK(f.terminal.info().focus == "clip-grid");
  CHECK(!f.terminal.info().planes.children.empty());
  CHECK(f.terminal.frame().rows() == 40);
}

TEST(terminal_ui_tab_switches_view_and_moves_focus) {
  UIFixture f;
  CHECK(f.terminal.text().find("File  Edit  Track") != std::string::npos);
  size_t before = f.terminal.frameCount();
  f.press('\t');
  CHECK(f.terminal.frameCount() > before);
  CHECK(f.terminal.text().find(" master") != std::string::npos);
  CHECK(f.terminal.text().find("00 │") != std::string::npos);
  CHECK(f.terminal.info().focus == "pattern-editor");
}

TEST(terminal_ui_follows_a_resize) {
  UIFixture f;
  f.terminal.resize(30, 90);
  f.ui->step();
  CHECK(f.terminal.frame().rows() == 30);
  CHECK(f.terminal.frame().cols() == 90);
}

// The pattern editor moves its cursor down the screen without scrolling until
// it comes within the scroll margin of the bottom, then scrolls to keep it
// there.
TEST(terminal_ui_scrolls_only_within_the_margin_of_the_bottom) {
  constexpr int kMargin = 3;
  UIFixture f(fixture("center_note.xml"), UI::View::ARRANGEMENT);
  f.otherWindow();  // the pattern editor
  auto rows = patternRows(f);
  size_t visible = rows.size();
  CHECK(visible > 2 * kMargin);
  CHECK(!rows.empty() && rows[0] == 0);

  size_t steps = 0;
  while (steps < 64 && patternRows(f)[0] == 0) {
    f.press(cellui::Key::Down);
    steps++;
  }
  CHECK(steps == visible - kMargin);
  int cursor_line = static_cast<int>(steps) - patternRows(f)[0];
  CHECK(cursor_line == static_cast<int>(visible) - 1 - kMargin);
}

// kill-region on an empty clip slot removes its stop button, a second press
// has nothing left, and toggle-stop-button brings it back.
TEST(terminal_ui_clip_grid_stop_button_is_removed_and_restored) {
  UIFixture f(fixture("terminal_ui_live.xml"), UI::View::LIVE);
  // The clip grid's first slot: columns after the 30-column outline panel.
  auto has_stop = [&] {
    std::string slot;
    for (int c = 30; c < 48; c++) slot += f.terminal.frame().at(2, c).text;
    return slot.find("\xE2\x8F\xB9") != std::string::npos;
  };
  CHECK(has_stop());
  f.press(cellui::Key::Delete);
  CHECK(!has_stop());
  f.press(cellui::Key::Delete);
  CHECK(!has_stop());
  f.execute("toggle-stop-button");
  CHECK(has_stop());
}

// set-song-scale lists the scales, choosing Otonal says so on the status line,
// and saving writes it to the song file.
TEST(terminal_ui_set_song_scale_lists_scales_and_saves_the_choice) {
  namespace fs = std::filesystem;
  auto song = fs::path(TESTS_SCRATCH_DIR) / "terminal_ui_scale_scratch.xml";
  fs::copy_file(fixture("center_note.xml"), song, fs::copy_options::overwrite_existing);
  auto read = [&] {
    std::ifstream in(song);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  };
  CHECK(read().find("scale=\"") == std::string::npos);

  UIFixture f(song.string(), UI::View::ARRANGEMENT);
  f.execute("set-song-scale");
  for (const char * name : { "None (chromatic)", "Major", "Minor", "Otonal", "Utonal" }) {
    CHECK(f.text().find(name) != std::string::npos);
  }
  // The list starts on the scale in use, None.
  for (int i = 0; i < 3; i++) f.press(cellui::Key::Down);
  f.press(cellui::Key::Enter);
  CHECK(f.text().find("Scale Otonal") != std::string::npos);

  f.ctrl('x');
  f.ctrl('s');
  CHECK(read().find("scale=\"otonal\"") != std::string::npos);
  fs::remove(song);
}

// A modal dialog frosts everything behind it, and closing it gives the screen
// back as it was.
TEST(terminal_ui_modal_dialog_shows_the_screen_behind_it_through_a_backdrop) {
  UIFixture f;
  f.terminal.emulateBackdrops(true); // as a terminal shows one
  auto menu = f.terminal.text().find("File");
  CHECK(menu != std::string::npos);
  auto sharp = f.terminal.frame().at(0, 0);
  auto hasScrim = [&] {
    for (auto & plane : f.terminal.info().planes.children)
      if (plane.name == "scrim") return true;
    return false;
  };
  CHECK(!hasScrim());

  f.execute("about");
  CHECK(hasScrim());
  auto red = [](const cellui::CellBuffer::Cell & cell) { return cell.fg.value_or(cellui::Color()).getRed(); };
  auto green = [](const cellui::CellBuffer::Cell & cell) { return cell.fg.value_or(cellui::Color()).getGreen(); };
  auto frosted = f.terminal.frame().at(0, 0);
  CHECK(frosted.fg.has_value() && sharp.fg.has_value());
  CHECK(red(frosted) != red(sharp) || green(frosted) != green(sharp));

  f.press(cellui::Key::Escape);
  CHECK(!hasScrim());
  auto back = f.terminal.frame().at(0, 0);
  CHECK(red(back) == red(sharp) && green(back) == green(sharp) && back.text == sharp.text);
}

// The app only sets what is behind a modal dialog aside; the backend applies
// the backdrop.
TEST(terminal_ui_modal_dialog_gives_the_backend_the_screen_behind_it_as_a_layer) {
  UIFixture f;
  CHECK(f.terminal.info().backdrops.empty());
  f.execute("about");
  const auto & layers = f.terminal.info().backdrops;
  CHECK(layers.size() == 1);
  CHECK(layers[0].backdrop.active());
  CHECK(layers[0].cells.toPlainText().find("File") != std::string::npos);
  CHECK(layers[0].cells.toPlainText().find("About") == std::string::npos);
  f.press(cellui::Key::Escape);
  CHECK(f.terminal.info().backdrops.empty());
}

// Where the terminal shows images stretched over cells, the backdrop is
// pictures of the screen behind, in tiles, and the dialog stays text.
TEST(terminal_ui_modal_backdrop_is_pictures_where_the_terminal_shows_them) {
  cellui::DisplayInfo display;
  display.pictures = true;
  display.cell_px_height = 20;
  display.cell_px_width = 10;
  UIFixture f("", UI::View::LIVE, display);
  f.terminal.emulateBackdrops(true);
  size_t before = f.terminal.images().size();
  f.execute("about");
  size_t stretched = 0;
  for (auto & p : f.terminal.images()) stretched += p.image->stretched();
  CHECK(stretched > 0);
  CHECK(f.terminal.images().size() > before);
  CHECK(f.terminal.frame().at(0, 0).image > 0);
  CHECK(f.text().find("About") != std::string::npos);
  f.press(cellui::Key::Escape);
  for (auto & p : f.terminal.images()) CHECK(!p.image->stretched());
}

// A backend that reports releases (a window, a kitty-protocol terminal) sends
// the release of the Enter that ran the command after the dialog is open; it
// must not close it.
TEST(terminal_ui_info_dialog_ignores_key_releases) {
  UIFixture f;
  f.execute("about");
  f.terminal.pushKey(cellui::Key::Enter, 0, cellui::Input::Kind::RELEASE);
  f.terminal.pushKey('q', 0, cellui::Input::Kind::RELEASE);
  f.ui->step();
  CHECK(f.terminal.info().focus == "info-dialog");
  f.terminal.pushKey(cellui::Key::Enter, 0, cellui::Input::Kind::PRESS);
  f.ui->step();
  CHECK(f.terminal.info().focus != "info-dialog");
}

// The about dialog leaves the mouse to the terminal, to select its text with,
// and so a click doesn't close it; it takes the mouse back when closed.
TEST(terminal_ui_info_dialog_leaves_the_mouse_to_the_terminal) {
  UIFixture f;
  CHECK(!f.terminal.nativeSelection());
  f.execute("about");
  CHECK(f.terminal.nativeSelection());
  f.terminal.pushMouse(20, 60, cellui::Key::Button1);
  f.ui->step();
  CHECK(f.terminal.info().focus == "info-dialog");
  f.press(cellui::Key::Escape);
  CHECK(!f.terminal.nativeSelection());
}

// The menu's open section floats at the overlay elevation, a modal dialog
// and its scrim above it at the modal one.
TEST(terminal_ui_overlays_and_modal_dialogs_float_at_their_elevations) {
  UIFixture f;
  const auto & elevation = cellui::Theme::dark().elevation;
  // The planes above the base, without the lifted cells (which have no name).
  auto planes = [&] {
    std::vector<cellui::ElevatedArea> named;
    for (const auto & area : f.terminal.info().elevations) {
      if (!area.name.empty()) named.push_back(area);
    }
    return named;
  };
  CHECK(planes().empty());
  f.meta('f');
  auto areas = planes();
  CHECK(areas.size() == 1 && areas[0].name == "menu-section" && areas[0].elevation == elevation.overlay);
  f.press(cellui::Key::Escape);
  f.press(cellui::Key::Escape);

  f.execute("about");
  // What the scrim blurs casts no shadow, lifted cells included.
  areas = f.terminal.info().elevations;
  CHECK(areas.size() == 2);
  CHECK(areas[0].name == "scrim" && areas[0].elevation == elevation.modal);
  CHECK(areas[1].elevation == elevation.modal && areas[1].rows < f.terminal.frame().rows());
  f.press(cellui::Key::Escape);
  CHECK(planes().empty());
}

// A choice dialog's entries show over its raised box.
TEST(terminal_ui_choice_dialog_entries_show_over_the_raised_box) {
  UIFixture f;
  f.ui->showChoiceDialog("Pick one", {{"Alpha entry", "a"}, {"Beta entry", "b"}}, 0, [](int) {});
  f.ui->step();
  CHECK(f.terminal.text().find("Pick one") != std::string::npos);
  CHECK(f.terminal.text().find("Alpha entry") != std::string::npos);
  CHECK(f.terminal.text().find("Beta entry") != std::string::npos);
}

// Live View: the clip grid's track headers stand half a cell above it and its
// clips a whole one; the pattern editor's track headers half a cell, a
// group's (the master's) a quarter.
TEST(terminal_ui_clips_and_track_headers_stand_on_their_widgets) {
  UIFixture f(fixture("terminal_ui_live.xml"), UI::View::LIVE);
  const auto & elevation = cellui::Theme::dark().elevation;
  std::vector<cellui::ElevatedArea> headers, groups, clips;
  for (const auto & area : f.terminal.info().elevations) {
    if (area.elevation == elevation.raised) headers.push_back(area);
    if (area.elevation == elevation.panel) groups.push_back(area);
    if (area.elevation == elevation.card) clips.push_back(area);
  }
  // One populated clip of eight; the empty slots stay flat.
  CHECK(clips.size() == 1 && clips[0].rows == 1);
  // The clip grid's one track header, and the pattern editor's: two lines,
  // one piece.
  CHECK(headers.size() == 2);
  CHECK(std::any_of(headers.begin(), headers.end(), [](const cellui::ElevatedArea & a) { return a.rows == 2; }));
  // The master's header in the clip grid, and its row over the tracks in the
  // pattern editor.
  CHECK(groups.size() == 2);
  for (const auto & area : f.terminal.info().elevations) CHECK(area.name.empty());
}
