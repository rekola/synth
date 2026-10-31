#pragma once

#include <cellui/term/TermEvent.h>

#include <chrono>
#include <deque>
#include <optional>
#include <string>

namespace cellui::term {

// Turns the bytes a terminal sends into events: keys in the legacy, xterm
// and kitty encodings, SGR and X10 mouse reports, bracketed paste, focus,
// and the replies to start-up queries. It follows ECMA-48's syntax rather
// than a table of known strings, so a complete sequence it does not know is
// consumed whole (an UNKNOWN event), never replayed as typed text.
//
// Bytes arrive in reads that can split a sequence anywhere. What is left
// incomplete at the end of a read is held for a short grace period (25 ms
// unless changed): more bytes within it continue the sequence, and when it
// expires the bytes so far are settled - a lone ESC becomes the Escape key,
// ESC [ becomes Alt+[, a broken UTF-8 character U+FFFD. The grace only
// reassembles what one burst from the terminal was split into; ESC followed
// later by a key acting as Meta is the key map's business, not this one's.
// Nothing here waits or reads a clock: the caller passes the time of each
// read, asks nextDeadline() when to call expire(), and takes the events.
class InputParser {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  InputParser();

  void feed(const char * data, size_t size, TimePoint now);
  void feed(const std::string & bytes, TimePoint now) { feed(bytes.data(), bytes.size(), now); }
  // Settles whatever is held if its deadline has passed.
  void expire(TimePoint now);
  // When expire() must be called even if no byte arrives.
  std::optional<TimePoint> nextDeadline() const { return deadline_; }
  // The input has ended: settles what is held and reports END.
  void finish();

  bool hasEvents() const { return !events_.empty(); }
  TermEvent pop();
  std::deque<TermEvent> takeAll();

  void setGrace(std::chrono::milliseconds grace) { grace_ = grace; }
  // Under the kitty keyboard protocol modifier parameters use kitty's bit
  // layout (8 is Super) rather than xterm's (8 is Meta).
  void setKittyKeyboard(bool on) { kitty_ = on; }
  // A mouse report outside the screen is dropped; 0 means not known.
  void setScreenSize(int rows, int cols) {
    screen_rows_ = rows;
    screen_cols_ = cols;
  }
  // Pixel mouse coordinates are divided by the cell size; 0 means unknown,
  // and pixel reports are then dropped.
  void setPixelMouse(bool on, int cell_px_height = 0, int cell_px_width = 0) {
    pixel_mouse_ = on;
    cell_px_h_ = cell_px_height;
    cell_px_w_ = cell_px_width;
  }
  // CSI row;col R is a cursor report only while one was asked for;
  // otherwise it is F3 with modifiers.
  void expectCursorReport() { cursor_reports_expected_++; }
  // CSI ? row ; col R is the reply to CSI ? 6 n, which nothing else sends;
  // it cannot be mistaken for a key but is only taken for one while it is
  // expected, as the repair below needs the count.
  void expectDecCursorReport() { dec_reports_expected_++; }
  // Forgets the replies still expected (the ones to a query that was never answered).
  void forgetExpectedReports() { cursor_reports_expected_ = dec_reports_expected_ = 0; }
  // While replies are expected, an Escape (or ESC [) that was settled before
  // the rest of a reply arrived is not lost with it: the late "[ ? row ; col R"
  // is taken as the reply and does not reach the application as text. The
  // Escape key already emitted stays emitted.
  int repliesExpected() const { return cursor_reports_expected_ + dec_reports_expected_; }
  // rxvt ends its colour replies with a bare ESC.
  void setRxvt(bool on) { rxvt_ = on; }
  void setPasteCap(size_t bytes) { paste_cap_ = bytes; }

 private:
  enum class State { GROUND, ESC, CSI, SS3, STRING_INTRO, STRING, STRING_ESC, UTF8, PASTE, X10, ORPHAN };
  // What a settled Escape may still turn out to have been the start of.
  enum class Orphan { NONE, AFTER_ESC, AFTER_CSI };

  void byte(unsigned char c);
  void ground(unsigned char c, bool alt);
  void emit(TermEvent e) {
    e.at = last_now_;
    events_.push_back(std::move(e));
  }
  void emitKey(int key, unsigned mods, TermEvent::Kind kind = TermEvent::Kind::UNKNOWN, std::string text = "");
  void emitControl(unsigned char c, bool alt);
  void dispatchCsi();
  void dispatchSs3(unsigned char final_byte);
  void dispatchString();
  bool dispatchKittyKey(const std::vector<std::vector<int>> & params);
  void dispatchTildeKey(const std::vector<std::vector<int>> & params);
  void dispatchLetterKey(char final_byte, const std::vector<std::vector<int>> & params);
  void dispatchSgrMouse(const std::vector<std::vector<int>> & params, bool release);
  void dispatchX10Mouse();
  void mouse(int code, int px, int py, bool release, bool pixels);
  unsigned legacyMods(int param) const;
  void pasteByte(unsigned char c);
  void flushPaste(bool continues);
  void settle();
  void reset();
  bool stringIntroPossible() const;

  State state_ = State::GROUND;
  std::deque<TermEvent> events_;
  std::optional<TimePoint> deadline_;
  std::chrono::milliseconds grace_{25};
  std::chrono::milliseconds string_grace_{1000};

  // CSI: the parameter and intermediate bytes so far.
  std::string params_, intermediates_;
  char final_ = 0;
  bool alt_pending_ = false;
  // The string type (']' 'P' '_') and its body.
  char string_type_ = 0;
  std::string string_;
  // A UTF-8 character's bytes so far and how many it needs.
  std::string utf8_;
  size_t utf8_need_ = 0;
  // The raw bytes of the sequence in progress, for UNKNOWN events.
  std::string raw_;
  std::string x10_;

  std::string paste_;
  size_t paste_match_ = 0;  // how much of the end marker has been seen
  bool paste_last_cr_ = false;
  size_t paste_cap_ = 1 << 20;

  bool kitty_ = false;
  bool rxvt_ = false;
  bool pixel_mouse_ = false;
  int cell_px_h_ = 0, cell_px_w_ = 0;
  int screen_rows_ = 0, screen_cols_ = 0;
  int cursor_reports_expected_ = 0, dec_reports_expected_ = 0;
  TimePoint last_now_{};
  Orphan orphan_ = Orphan::NONE;
  TimePoint orphan_until_{};
  bool ended_ = false;
};

}  // namespace cellui::term
