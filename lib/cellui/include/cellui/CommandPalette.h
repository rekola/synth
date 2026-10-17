#pragma once

#include <cellui/ChoiceList.h>
#include <cellui/Input.h>
#include <cellui/UIPlane.h>

#include <memory>
#include <string>
#include <vector>

namespace cellui {

// One command the palette can run.
struct PaletteEntry {
  std::string name;
  std::string description;
  // How it is bound in the current keymap, "" if it is not.
  std::string binding;
};

// A modal command prompt on a raised panel: a query line, the commands that
// fuzzy-match it best first with their keybindings, and the selected one's
// description below. Typing edits the query; Up/Down (and the Emacs-style
// next/previous-line keys) move; Tab completes the query as far as every
// command starting with it agrees, as in the Emacs minibuffer; Enter or a
// click chooses; Escape or a click outside cancels. The caller runs the chosen command.
class CommandPalette {
 public:
  enum class Result { NONE,
                      CHOSEN,
                      CANCELLED };

  bool isOpen() const { return open_; }

  // Nothing is drawn until show().
  void open(std::vector<PaletteEntry> entries);
  void close();
  void raise() {
    if (plane_) plane_->moveToTop();
  }

  // (Re)draws the box under `parent`, centred on a screen of the given size.
  // Call again after a resize or any change.
  void show(UIPlane & parent, int screen_rows, int screen_cols);

  // Feeds one input event; the palette swallows everything, so the caller
  // only needs the result.
  Result offerInput(const Input & input);

  // The selected command, nullptr when nothing matches.
  const PaletteEntry * chosen() const;
  const std::string & query() const { return query_; }
  // What the last Tab had to say ("[No match]"), "" if nothing.
  const std::string & note() const { return note_; }
  // The names shown, best match first.
  std::vector<std::string> shownNames() const;

 private:
  struct Shown {
    size_t entry;
    std::vector<int> positions;
  };
  void refilter();
  void complete();
  int visibleRows() const { return rows_ - 6; }

  bool open_ = false;
  std::unique_ptr<UIPlane> plane_;
  std::vector<PaletteEntry> entries_;
  std::string query_;
  // "[No match]" or "[Sole completion]" after a Tab, until the next edit.
  std::string note_;
  std::vector<Shown> shown_;
  ChoiceList list_;
  int y_ = 0, x_ = 0, rows_ = 0, width_ = 0;
};

}  // namespace cellui
