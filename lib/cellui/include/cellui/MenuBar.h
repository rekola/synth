#pragma once

#include <cellui/Color.h>
#include <cellui/Input.h>
#include <cellui/Plane.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cellui {

// A menu bar drawn into a plane of its own that spans its parent's width at
// the top: a header row of section names, and below it the open section as
// an opaque panel of items, with no border, on a child plane floating at the
// theme's overlay elevation. The plane is as tall as the header plus the
// longest section and its padding row; cells nothing is drawn in stay
// transparent, so only the header and an open section show.
//
//   - Alt+<mnemonic>, or a click on a section's name, opens it; a click on
//     the open one closes it.
//   - While one is open: Left/Right move between sections, Up/Down and the
//     wheel between items (both wrap, skipping separators and disabled
//     items), Escape closes, Enter or a click on an item activates it.
//
// It knows nothing of commands: takeActivated() says which item was
// activated, and the owner runs whatever that item means.
class MenuBar {
 public:
  struct Item {
    std::string label;  // empty with `separator`
    bool separator = false;
    bool enabled = true;
  };
  struct Section {
    std::string name;
    int mnemonic = 0;  // Alt+this opens the section; its first occurrence in the name is marked
    std::vector<Item> items;
  };
  struct Colors {
    Color header_fg = Color(255, 255, 255), header_bg = Color(0x7f, 0x34, 0x7f);
    Color section_fg = Color(255, 255, 255), section_bg = Color(0, 0, 0);
    Color disabled_fg = Color(0xdd, 0xdd, 0xdd);
    // The highlighted item.
    Color selected_fg = Color(0, 0, 0), selected_bg = Color(255, 255, 255);
  };

  explicit MenuBar(Plane & plane) : plane_(plane) { }
  MenuBar(Plane & plane, Colors colors) : plane_(plane), colors_(colors) { }

  // Replaces the sections; nothing stays open.
  void setSections(std::vector<Section> sections);
  const std::vector<Section> & sections() const { return sections_; }

  // Sizes the plane to its parent's width and draws.
  void layout(int parent_cols);
  void draw();
  // Takes effect on the next draw().
  void setColors(const Colors & colors) { colors_ = colors; }

  // Whether the input was the menu's.
  bool offerInput(const Input & input);

  void open(int section);
  void close();
  bool isOpen() const { return open_ >= 0; }
  int openSection() const { return open_; }
  // The highlighted item of the open section, -1 if none.
  int selectedItem() const { return open_ >= 0 ? selected_ : -1; }
  // The (section, item) the last offerInput() activated; cleared by the read.
  std::optional<std::pair<int, int>> takeActivated();

  // Where each section's name starts on the header row.
  int sectionColumn(int section) const;

 private:
  bool selectable(int section, int item) const;
  void step(int delta);
  void stepSection(int delta);
  int boxColumn() const;
  int bodyWidth(int section) const;

  Plane & plane_;
  std::unique_ptr<Plane> panel_;  // the open section's
  Colors colors_;
  std::vector<Section> sections_;
  int cols_ = 0;
  int open_ = -1;
  int selected_ = -1;
  std::optional<std::pair<int, int>> activated_;
};

}  // namespace cellui
