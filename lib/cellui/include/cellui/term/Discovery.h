#pragma once

#include <cellui/term/Capabilities.h>
#include <cellui/term/TermEvent.h>

#include <chrono>
#include <string>

namespace cellui::term {

// Start-up negotiation: the queries to send, and the capability record
// built from whatever replies arrive. Replies come through InputParser as
// REPLY events; the primary device attributes reply, asked for last, marks
// the end, since every terminal answers it. A terminal that never answers
// leaves discovery incomplete and the caller stops waiting at deadline(),
// with a conservative record. Nothing here does any I/O.
class Discovery {
 public:
  explicit Discovery(Environment env);

  // The queries, in order. The caller first enters the alternate screen,
  // so bytes a terminal fails to consume land there. The first query is a
  // cursor position report: tell the parser to expect one.
  std::string queries() const;

  void ingest(const TermEvent & reply);
  // The cell size the kernel's window size gives, for a terminal that does
  // not answer the pixel queries (xterm refuses them by default).
  void setWindowCellPixels(int height, int width) {
    win_cell_px_h_ = height;
    win_cell_px_w_ = width;
  }
  bool complete() const { return complete_; }

  // How long to wait for the replies: longer over ssh.
  std::chrono::milliseconds timeout() const;

  // The record, from what has arrived so far.
  Capabilities result() const;

 private:
  // Identification queries make some terminals print garbage; these are
  // identified from the environment instead.
  bool preIdentified() const;

  Environment env_;
  bool complete_ = false;
  bool cursor_report_ = false, dec_cursor_report_ = false;

  std::string da3_name_;
  std::string xtversion_name_, xtversion_version_;
  std::string tn_name_;
  bool tcap_rgb_ = false, tcap_hpa_ = false;
  int da2_pp_ = -1, da2_pv_ = -1;
  std::optional<Color> default_fg_, default_bg_;
  bool kitty_keyboard_ = false;
  bool sync_ = false;
  bool pixel_mouse_ = false;
  int sixel_registers_ = 0, sixel_max_w_ = 0, sixel_max_h_ = 0;
  bool da1_sixel_ = false;
  bool kitty_graphics_ = false;
  int text_px_h_ = 0, text_px_w_ = 0, cell_px_h_ = 0, cell_px_w_ = 0;
  int win_cell_px_h_ = 0, win_cell_px_w_ = 0;
  int rows_ = 0, cols_ = 0;
};

}  // namespace cellui::term
