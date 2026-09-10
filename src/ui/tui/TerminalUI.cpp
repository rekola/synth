#include "TerminalUI.h"

#include "../../playback/InputEvent.h"
#include "../../Controller.h"
#include "../UIMenu.h"
#include "../Chart.h"
#include "../HeatmapChart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "PatternEditor.h"
#include "ArrangementGrid.h"
#include "SessionView.h"
#include "OutlineView.h"
#include "CoverArt.h"
#include "SpinBox.h"
#include "../../dsp/DiracAnalyzer.h"
#include "../../audio/AudioAPI.h"
#include "../../launchpad/LaunchpadIO.h"
#include "../../launchpad/LaunchpadPadEvent.h"
#include "../../launchpad/LaunchpadButtonEvent.h"
#include "../../launchpad/LaunchpadChannelPressureEvent.h"
#include "../../launchpad/LaunchpadProtocol.h"
#include "../../launchpad/LaunchpadManager.h"
#include "EscapeCoalescer.h"
#include "NotcursesInputEventSource.h"
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
#include "../../playback/ThresholdRecordingTriggeredEvent.h"
#include "../../playback/PlaybackControlEvent.h"
#include "../../playback/AudioBlockEvent.h"
#include "../../playback/VisualizationResultEvent.h"
#include "../../playback/VisualizationThread.h"
#include "../../bus/BusEffectRegistry.h"

#include <cstdio>
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

#include <ncpp/NotCurses.hh>
#include <ncpp/Plane.hh>
#include <ncpp/Plot.hh>
#include <ncpp/Reader.hh>
#include <ncpp/Menu.hh>
#include <ncpp/Selector.hh>
#include <ncpp/Visual.hh>

#include <poll.h>

using namespace ncpp;
using namespace std;
using namespace fmt;

// The inverse of readInput()'s ni.evtype -> InputEvent::Kind mapping below -
// needed because ncmenu_offer_input() only treats a mouse click on the menu
// bar as consumed when evtype is specifically NCTYPE_RELEASE (confirmed
// against the real library: NCTYPE_UNKNOWN and NCTYPE_PRESS are both
// silently ignored). Hardcoding NCTYPE_UNKNOWN here - discarding the real
// press/release Kind InputEvent already carries - meant a mouse click could
// never open the File menu on any terminal, mouse-protocol support
// notwithstanding.
static inline ncintype_e to_ncintype(InputEvent::Kind kind) {
  switch (kind) {
  case InputEvent::Kind::PRESS: return NCTYPE_PRESS;
  case InputEvent::Kind::REPEAT: return NCTYPE_REPEAT;
  case InputEvent::Kind::RELEASE: return NCTYPE_RELEASE;
  default: return NCTYPE_UNKNOWN;
  }
}

// use_true_case is for an active reader (track name/annotation/M-x text
// entry) only: TerminalUI::readInput()'s dispatchRawKey unconditionally
// lowercases a plain letter's own id (needed for case-insensitive
// keybinding dispatch and note-entry - see InputEvent's own comment on
// getTrueCaseId()), so a menu/selector - which do want that normalized
// form, for consistent mnemonic matching regardless of caps lock -
// keep passing false (the default) here. A reader cares about neither;
// it wants whatever was actually typed. id itself barely matters for a
// reader either way (ncreader_offer_input() only compares it against
// Backspace/arrow-key/synthesized ids, never a letter), but eff_text is
// what its own plain-character insertion path actually reads character
// content from (see reader.c's own ncreader_offer_input()) - true case
// has to reach *that* field for a reader to ever see a real uppercase
// letter.
static inline ncinput to_ncinput(const InputEvent & input, bool use_true_case = false) {
  auto case_id = use_true_case ? input.getTrueCaseId() : input.getId();
  ncinput ni = { .id = static_cast<uint32_t>(case_id), .y = input.getY(), .x = input.getX(), .utf8 = { 0, 0, 0, 0, 0 }, .alt = input.hasAlt(), .shift = input.hasShift(), .ctrl = input.hasCtrl(), .evtype = to_ncintype(input.getKind()), .modifiers = static_cast<uint32_t>((input.hasAlt() ? NCKEY_MOD_ALT : 0) | (input.hasCtrl() ? NCKEY_MOD_CTRL : 0) | (input.hasShift() ? NCKEY_MOD_SHIFT : 0) | (input.hasMeta() ? NCKEY_MOD_META : 0)), .ypx = -1, .xpx = -1 };
  // eff_text was added in notcurses 3.0.10 - Ubuntu 24.04's packaged
  // 3.0.7 (what CI builds against) predates it, so this field can't be
  // set unconditionally; NCINPUT_MAX_EFF_TEXT_CODEPOINTS, defined right
  // next to it, doubles as its own feature-test macro.
#ifdef NCINPUT_MAX_EFF_TEXT_CODEPOINTS
  ni.eff_text[0] = static_cast<uint32_t>(case_id);
#endif
  return ni;
}

// ncreader has no "set full contents" call - only single-EGC writes - so
// seeding/replacing its contents means feeding `text`'s own UTF-8
// codepoints one at a time. Splitting on codepoint boundaries rather than
// full Unicode grapheme clusters (combining marks/ZWJ sequences would each
// become a separate write instead of one grouped EGC) is an acceptable
// first-pass approximation for plain annotation/command text - see
// PatternEditor's own annotation-editing entry points and StatusLine's M-x
// autocomplete.
static inline void writeEgcString(ncreader * reader, const string & text) {
  size_t i = 0;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    len = std::min(len, text.size() - i);
    ncreader_write_egc(reader, text.substr(i, len).c_str());
    i += len;
  }
}

static inline long long now() {
  struct timeval tv;
  int r = gettimeofday(&tv, 0);
  if (r == 0) {
    return (long long)1000 * tv.tv_sec + tv.tv_usec / 1000;
  } else {
    return 0;
  }
}

class TerminalPlane : public UIPlane {
public:
  TerminalPlane(std::shared_ptr<Controller> & _controller, Plane * _plane, bool _owner = true) : UIPlane(_controller), plane(_plane), owner(_owner) {
    unsigned int y, x;
    plane->get_dim(&y, &x);
    setDim(pair(static_cast<int>(y), static_cast<int>(x)));
    setPosition(pair(0, 0));
    // A terminal's own capabilities don't change mid-session - figured
    // out once, here, rather than re-querying notcurses on every render.
    sextant_support_ = notcurses_cansextant(ncplane_notcurses_const(plane->to_ncplane()));
  }
  ~TerminalPlane() {
    if (owner) delete plane;
  }
  void resize(int rows, int cols) override {
    if (plane->to_ncplane()) {
      UIPlane::resize(rows, cols);
      plane->resize(rows, cols);
    }
  }
  void move(int y, int x) override {
    if (plane->to_ncplane()) {
      UIPlane::move(y, x);
      plane->move(y, x);
    }
  }
  void moveToTop() override {
    if (plane->to_ncplane()) plane->move_top();
  }
  void setFgColor(int r, int g, int b) override { plane->set_fg_rgb8(r, g, b); }
  void setBgColor(int r, int g, int b) override { plane->set_bg_rgb8(r, g, b); }
  void setUnderline(bool b) override {
    if (b) {
      plane->styles_set(CellStyle::Underline);
    } else {
      plane->styles_set(CellStyle::None);
    }
  }
  void setBold(bool b) override {
    if (b) {
      plane->styles_set(CellStyle::Bold);
    } else {
      plane->styles_set(CellStyle::None);
    }
  }
  void setItalic(bool b) override {
    if (b) {
      plane->styles_set(CellStyle::Italic);
    } else {
      plane->styles_set(CellStyle::None);
    }
  }
  void erase() override { plane->erase(); }
  void putstr(int y, int x, const std::string & s) override { plane->putstr(y, x, s.c_str()); }
  unique_ptr<UIPlane> createChild() override {
    auto child_plane = new Plane(1, 1, 0, 0);
    child_plane->set_base("", 0, NCCHANNELS_INITIALIZER(0xc0, 0x80, 0xc0, 0x20, 0, 0x20));
    return make_unique<TerminalPlane>(getController(), child_plane);
  }
  
  void drawBorder() override {
    plane->erase();

    unsigned fg_red, fg_green, fg_blue;
    plane->get_fg_rgb8(&fg_red, &fg_green, &fg_blue);

    unsigned bg_red, bg_green, bg_blue;
    plane->get_bg_rgb8(&bg_red, &bg_green, &bg_blue);

    auto channels = NCCHANNELS_INITIALIZER(fg_red, fg_green, fg_blue, bg_red, bg_green, bg_blue);
    
    nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
    nccell lr = NCCELL_TRIVIAL_INITIALIZER, ll = NCCELL_TRIVIAL_INITIALIZER;
    nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;
    if (nccells_rounded_box(plane->to_ncplane(), NCSTYLE_NONE, 0, &ul, &ur, &ll, &lr, &hl, &vl)) {
      return;
    }
    ul.channels = ur.channels = ll.channels = lr.channels = hl.channels = vl.channels = channels;
    nccell_set_bg_alpha(&ul, NCALPHA_BLEND);
    nccell_set_bg_alpha(&ur, NCALPHA_BLEND);
    nccell_set_bg_alpha(&ll, NCALPHA_BLEND);
    nccell_set_bg_alpha(&lr, NCALPHA_BLEND);
    nccell_set_bg_alpha(&hl, NCALPHA_BLEND);
    nccell_set_bg_alpha(&vl, NCALPHA_BLEND);
    
    if (ncplane_perimeter(plane->to_ncplane(), &ul, &ur, &ll, &lr, &hl, &vl, 0)) {
      nccell_release(plane->to_ncplane(), &ul); nccell_release(plane->to_ncplane(), &ur); nccell_release(plane->to_ncplane(), &hl);
      nccell_release(plane->to_ncplane(), &ll); nccell_release(plane->to_ncplane(), &lr); nccell_release(plane->to_ncplane(), &vl);
      return;
    }
    nccell_release(plane->to_ncplane(), &ul); nccell_release(plane->to_ncplane(), &ur); nccell_release(plane->to_ncplane(), &hl);
    nccell_release(plane->to_ncplane(), &ll); nccell_release(plane->to_ncplane(), &lr); nccell_release(plane->to_ncplane(), &vl);
  }
  
  // y/x/rows/cols let a caller that isn't a one-line plane (PatternEditor's
  // annotation editing, which needs the reader positioned at the cursor's
  // own screen row, not row 0) place and size the reader plane explicitly.
  // x == -1/rows == -1/cols == -1 (the defaults) reproduce exactly what
  // this used to hardcode - StatusLine's existing calls are untouched.
  void showReader(const std::string & prompt = "", int y = 0, int x = -1, int rows = -1, int cols = -1,
		   const std::string & initial_text = "",
		   int text_r = 0xc0, int text_g = 0x80, int text_b = 0xc0) override {
    if (!readerActive()) {
      setOwning(false);

      // Erase this plane's own stale content on row y, from column x
      // onward (e.g. a previous status message longer than the new
      // prompt) before drawing the prompt - the reader plane created
      // below only ever covers its own bounds and its own cells only
      // get real content where the user has actually typed so far, so
      // anything left over underneath/beyond that was otherwise still
      // visible right through it (confirmed via a standalone
      // reproduction against the real library: opening the reader over
      // a long previous message left its stale tail visible past the
      // cursor until enough was typed to physically overwrite it).
      // Scoped to just this one row, starting at x (0 when unset, same
      // as reader_x's own default below) and reaching the plane's own
      // right edge, via ncplane_erase_region() rather than erase() (the
      // whole plane) - StatusLine passes x/y as their unset defaults,
      // always starting from this row's very first column same as
      // before, but PatternEditor's annotation editor
      // (startAnnotationEdit()) reuses this same call on its own much
      // taller plane with an explicit x placed after real pattern grid
      // content on that row, which a whole-row-from-0 erase wiped out
      // from under the reader right along with the actual stale content
      // past it. xlen must be an explicit positive width, not 0 - unlike
      // ylen, an xlen of 0 means "ignore xstart, erase the entire row's
      // width regardless" (see ncplane_erase_region()'s own doc comment)
      // and would put column 0 straight back in scope.
      auto erase_cols = getDim().second;
      auto erase_xstart = x == -1 ? 0 : x;
      ncplane_erase_region(getPlane().to_ncplane(), y, erase_xstart, 1, std::max(erase_cols - erase_xstart, 1));
      // The reader plane below is opaque and covers its own bounds, so any
      // prompt text must be drawn onto *this* (the still-visible underlying
      // plane) first, and the reader plane offset past it - otherwise the
      // prompt is drawn then immediately hidden under the reader, and the
      // whole M-x minibuffer silently looks like it never opened even
      // though it's actually active and correctly accepting input.
      if (!prompt.empty()) putstr(y, 0, prompt);
      auto prompt_width = static_cast<unsigned int>(prompt.size());

      ncreader_options reader_opts;
      // text_r/g/b (pink by default), matching the reader plane's own
      // colors below - see that comment for why. Background alpha
      // TRANSPARENT too, for the same reason: the typed glyphs themselves
      // shouldn't paint an opaque patch behind them either.
      reader_opts.tchannels = NCCHANNELS_INITIALIZER(static_cast<unsigned>(text_r), static_cast<unsigned>(text_g), static_cast<unsigned>(text_b), 0x00, 0x00, 0x00);
      ncchannels_set_fg_alpha(&reader_opts.tchannels, NCALPHA_HIGHCONTRAST);
      ncchannels_set_bg_alpha(&reader_opts.tchannels, NCALPHA_TRANSPARENT);
      reader_opts.tattrword = 0; // attributes used for input
      reader_opts.flags = NCREADER_OPTION_CURSOR | NCREADER_OPTION_HORSCROLL;

      auto [plane_rows, plane_cols] = getDim();
      auto reader_x = x == -1 ? static_cast<int>(prompt_width) : x;
      auto reader_rows = rows == -1 ? static_cast<unsigned int>(plane_rows) : static_cast<unsigned int>(rows);
      unsigned int reader_cols;
      if (cols == -1) {
	reader_cols = static_cast<unsigned int>(plane_cols) > prompt_width ?
	  static_cast<unsigned int>(plane_cols) - prompt_width : 1u;
      } else {
	// NOT widened past the caller's own requested on-screen width,
	// despite ncreader_contents() (src/lib/reader.c) reading back from
	// this exact plane rather than the wider offscreen buffer
	// HORSCROLL scrolls within - meaning typed text that outgrows this
	// plane's own width is genuinely unrecoverable through that call
	// (confirmed via a standalone reproduction against the real
	// library) - a first attempt at widening this plane to work around
	// that regressed much worse: every ncreader_redraw() (i.e. every
	// keystroke, not just this call) recopies its *entire* width from
	// the offscreen textarea, so any column beyond what's actually
	// been typed so far gets overwritten with blank cells on every
	// redraw - widening this plane past the caller's own narrow slot
	// meant every keystroke re-blanked everything to its right (other
	// tracks' own headings, Mute/Solo, ...), not just once at open
	// time the way showReader()'s own one-time erase above did. Left
	// at the caller's own width; the truncation above is a known,
	// accepted limitation until a real fix (e.g. tracking typed
	// content independently of ncreader_contents()) is worth the cost.
	reader_cols = cols > 0 ? static_cast<unsigned int>(cols) : 1u;
      }

      ncplane_options opts = {
	.y = y,
	.x = reader_x,
	.rows = reader_rows,
	.cols = reader_cols,
	.userptr = nullptr,
	.name = nullptr,
	.resizecb = nullptr,
	.flags = 0,
	.margin_b = 0,
	.margin_r = 0
      };

      auto reader_plane = ncplane_create(getPlane().to_ncplane(), &opts);
      // text_r/g/b, defaulting to the same pink createChild() already
      // gives every other UI plane's own base cell, so by default the
      // M-x prompt/completion indicator (drawn directly on the
      // surrounding StatusLine plane, not this one) and the typed text
      // here read as one consistent color instead of the reader's own
      // text standing out in an unrelated green - a caller whose reader
      // sits over its own non-default backdrop (PatternEditor's
      // track-name editor) overrides it instead for whatever actually
      // reads well there. Background alpha TRANSPARENT rather than an
      // explicit opaque color: this plane no longer paints its own
      // background at all, letting whatever's actually behind it show
      // straight through instead of a guessed-at literal color that may
      // not match. Set on both the base cell (below, for the plane's own
      // unwritten cells) and tchannels (above, for the glyphs ncreader
      // actually echoes as typed) so neither path leaves a stray opaque
      // patch.
      ncplane_set_fg_rgb8(reader_plane, static_cast<unsigned>(text_r), static_cast<unsigned>(text_g), static_cast<unsigned>(text_b));
      uint64_t base_channels = NCCHANNELS_INITIALIZER(static_cast<unsigned>(text_r), static_cast<unsigned>(text_g), static_cast<unsigned>(text_b), 0, 0, 0);
      ncchannels_set_bg_alpha(&base_channels, NCALPHA_TRANSPARENT);
      ncplane_set_base(reader_plane, " ", 0, base_channels);
      reader = ncreader_create(reader_plane, &reader_opts);
      writeEgcString(reader, initial_text);
      // showReaderIndicator()'s own `x` is a column *within the typed
      // text* (0 at the reader's own left edge), but its indicator_plane
      // is parented to this same plane (getPlane(), not reader_plane), so
      // it needs reader_x added back in to land at the right *absolute*
      // column - without this the indicator draws prompt_width columns
      // too far left, encroaching on the prompt/already-typed text
      // instead of sitting right after the cursor.
      reader_text_x = reader_x;
    }
  }

  bool readerActive() const override { return reader != 0; }

  string closeReader() override {
    char* contents;
    ncreader_destroy(reader, &contents);
    string r = contents;
    free(contents);
    reader = 0;
    if (indicator_plane) {
      ncplane_destroy(indicator_plane);
      indicator_plane = nullptr;
    }
    return r;
  }

  string getReaderContents() const override {
    if (!reader) return "";
    char * contents = ncreader_contents(reader);
    string r = contents ? contents : "";
    free(contents);
    return r;
  }

  void setReaderContents(const string & text) override {
    if (!reader) return;
    ncreader_clear(reader);
    writeEgcString(reader, text);
  }

  // A plane of its own, not drawn onto the reader's - see this method's
  // own doc comment on UIPlane.h for why (writing onto the reader's own
  // plane, even blank/erasing spaces, was confirmed to corrupt what
  // ncreader_contents()/getReaderContents() itself reports back as
  // typed). Created lazily on first use, positioned/erased/reshown fresh
  // each call - moving an existing plane and re-drawing its (freshly
  // erased) content is cheap, so there's no need to track whether this is
  // a reposition of an already-visible indicator or a first appearance.
  void showReaderIndicator(int x, const string & s) override {
    if (!reader) return;
    // `x` is a column within the *typed text* (0 at the reader's own left
    // edge) - this plane is parented to getPlane() (the same StatusLine
    // plane the reader itself is offset within), not to reader_plane, so
    // it needs reader_text_x added back in to land at the right absolute
    // column - see showReader()'s own comment on that member.
    auto abs_x = reader_text_x + x;
    if (!indicator_plane) {
      ncplane_options opts = {
	.y = 0, .x = abs_x, .rows = 1, .cols = kIndicatorPlaneCols,
	.userptr = nullptr, .name = nullptr, .resizecb = nullptr,
	.flags = 0, .margin_b = 0, .margin_r = 0
      };
      indicator_plane = ncplane_create(getPlane().to_ncplane(), &opts);
      // White, not the reader's own pink - a deliberate contrast so the
      // indicator (this app's own message) doesn't read as more of the
      // user's typed input.
      ncplane_set_fg_rgb8(indicator_plane, 0xff, 0xff, 0xff);
      uint64_t base_channels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0, 0, 0);
      ncchannels_set_bg_alpha(&base_channels, NCALPHA_TRANSPARENT);
      ncplane_set_base(indicator_plane, " ", 0, base_channels);
    }
    ncplane_move_yx(indicator_plane, 0, abs_x);
    ncplane_erase(indicator_plane);
    ncplane_putstr_yx(indicator_plane, 0, 0, s.c_str());
    ncplane_move_top(indicator_plane);
  }

  void hideReaderIndicator() override {
    // Destroyed, not just erased: an erased-but-still-present plane keeps
    // sitting raised above the reader (showReaderIndicator()'s own
    // ncplane_move_top()), and even a blank cell there is still real
    // content at that z-order - it keeps blocking whatever the reader
    // itself draws underneath afterward (e.g. the next character actually
    // typed there), rather than getting out of the way entirely.
    // showReaderIndicator() already creates it lazily on demand, so
    // there's nothing to reinitialize by destroying it here.
    if (indicator_plane) {
      ncplane_destroy(indicator_plane);
      indicator_plane = nullptr;
    }
  }

  // A genuine child plane of its own (positioned/sized by the caller),
  // not getPlane() itself - Selector's constructor calls
  // take_plane_ownership() on whatever Plane it's given, which would
  // otherwise hand over (and eventually destroy) this widget's own real
  // rendering plane. Mirrors showReader()'s own raw ncplane_create()
  // pattern above.
  void showPicker(int y, int x, int rows, int cols, int item_count) override {
    if (selector) return; // already active - see UIPlane.h's own comment
    // Dark background/light foreground matching this app's own theme
    // (StyleProvider isn't reachable from here - see TerminalMenu's own
    // header/sectionchannels for the identical precedent of hardcoding
    // colors directly rather than threading StyleProvider this deep) -
    // left at 0 (notcurses's own "no color set" default), these read as
    // full-bright white-on-whatever's-already-there instead, not this
    // app's dark theme.
    //
    // opchannels' own fg is deliberately the same bright green
    // StyleProvider's highlight_bg_color already uses everywhere else in
    // this codebase for "the current selection" (PatternEditor's/
    // OutlineView's own row cursor); its own bg is styles.highlight_fg_color's
    // matching true black, not a mid-grey (which read as washed-out,
    // hard-to-read text) - not arbitrary colors either way. The currently-
    // highlighted row (src/lib/selector.c's own render loop) gets no
    // channels of its own at all: it's opchannels' fg/bg *swapped*, so
    // this exact black-on-green pair is what that row shows, matching
    // that same established "current selection" look exactly (not just
    // approximating it).
    uint64_t opchannels = NCCHANNELS_INITIALIZER(0xa0, 0xff, 0xa0, 0x00, 0x00, 0x00);
    uint64_t boxchannels = NCCHANNELS_INITIALIZER(0x60, 0x60, 0x60, 0x20, 0x20, 0x20);
    // descchannels deliberately its own dark/neutral pair, not opchannels
    // again - every item's own .desc is always empty (addItem() callers
    // pass only an id), but ncselector.c's draw loop still unconditionally
    // prints a literal leading space in descchannels' own colors before
    // it ("%s" -> " %s") - sharing opchannels there would extend the
    // swapped highlight one cell past the option text itself on the
    // current row, a stray colored space right after it.
    uint64_t descchannels = NCCHANNELS_INITIALIZER(0x20, 0x20, 0x20, 0x20, 0x20, 0x20);
    // opchannels' own alpha left OPAQUE on both sides (not BLEND like
    // box/base/descchannels below) - BLEND on its bg word made the
    // highlighted row's text (that word, swapped into the fg slot) read
    // as a soft blended grey-green instead of a crisp black, despite the
    // RGB already being pure black above.
    ncchannels_set_bg_alpha(&descchannels, NCALPHA_BLEND);
    ncchannels_set_fg_alpha(&descchannels, NCALPHA_BLEND);
    ncchannels_set_bg_alpha(&boxchannels, NCALPHA_BLEND);
    ncchannels_set_fg_alpha(&descchannels, NCALPHA_BLEND);
    ncchannels_set_bg_alpha(&descchannels, NCALPHA_BLEND);
    ncselector_options opts =
      {
       .title = nullptr,
       .secondary = nullptr,
       .footer = nullptr,
       .items = nullptr,
       .defidx = 0,
       // Pinned to the exact item count, not 0 ("use all available
       // space") - see this method's own doc comment on UIPlane.h for
       // why 0 shows as blank padding rows above/below a tight list.
       .maxdisplay = static_cast<unsigned>(std::max(0, item_count)),
       .opchannels = opchannels,
       .descchannels = descchannels,
       .titlechannels = opchannels,
       .footchannels = opchannels,
       .boxchannels = boxchannels,
       .flags = 0
      };
    ncplane_options popts = {
      .y = y,
      .x = x,
      .rows = static_cast<unsigned>(std::max(1, rows)),
      .cols = static_cast<unsigned>(std::max(1, cols)),
      .userptr = nullptr,
      .name = nullptr,
      .resizecb = nullptr,
      .flags = 0,
      .margin_b = 0,
      .margin_r = 0
    };
    auto * picker_ncplane = ncplane_create(getPlane().to_ncplane(), &popts);
    // Explicit dark base cell too, so any cell ncselector's own drawing
    // never touches (e.g. this plane's own rows past a maxdisplay-limited
    // body, now shorter than a generously-sized `rows`) still reads as
    // this app's dark background rather than the terminal's own default -
    // BLEND alpha, matching opchannels/boxchannels above, so it's the
    // same partial transparency, not an opaque patch around the edges of
    // an otherwise-translucent popup.
    uint64_t base_channels = NCCHANNELS_INITIALIZER(0xc0, 0xc0, 0xc0, 0x20, 0x20, 0x20);
    ncchannels_set_bg_alpha(&base_channels, NCALPHA_BLEND);
    ncplane_set_base(picker_ncplane, " ", 0, base_channels);
    // A freshly created plane already sits above its parent by default,
    // but raised explicitly anyway - the whole point of a real overlay
    // plane, rather than text drawn inline, is that it's unambiguously on
    // top regardless of whatever else this or a sibling widget draws
    // later in the same frame (TerminalMenu's own raiseToTop() makes the
    // same guarantee for its own dropdown, for the same reason).
    ncplane_move_top(picker_ncplane);
    // A stack-local wrapper, not a heap one: Selector's constructor calls
    // take_plane_ownership() on it (Plane::release_native_plane(), so its
    // own destructor at the end of this scope becomes a no-op) - nothing
    // left to clean up once construction returns.
    Plane wrapper(picker_ncplane);
    selector = make_unique<Selector>(wrapper, &opts);
  }

  void addItem(const string & id, const string & description) override {
    if (!selector) return;
    // Unlike ncmenu_item's own .desc field (see TerminalMenu::rebuild()'s
    // comment), ncselector_additem() (src/lib/selector.c) strdup()s both
    // strings into its own storage immediately - a plain c_str() straight
    // off these locals is safe, nothing needs to outlive this call.
    ncselector_item item = { .option = id.c_str(), .desc = description.c_str() };
    selector->additem(&item);
  }

  // See UIPlane.h's own comment on why this - not a nonzero defidx at
  // create time - is how a picker starts highlighted somewhere other
  // than row 0.
  void selectPickerItem(int index) override {
    if (!selector) return;
    for (int i = 0; i < index; i++) selector->nextitem();
  }

  bool pickerActive() const override { return selector != nullptr; }

  string getPickerSelection() const override {
    if (!selector) return "";
    auto * s = selector->get_selected();
    return s ? s : "";
  }

  void closePicker() override {
    selector.reset();
  }

  bool offerInput(const InputEvent & input) override {
    if (reader) {
      // Confirmed bug in ncreader's own do_backspace() (src/lib/reader.c),
      // isolated in a standalone reproduction against this exact linked
      // library (libnotcurses-core.so.3.0.17) independent of anything in
      // this app: erasing a buffer's *last remaining* character is a
      // silent no-op - ncreader_contents() still reports the old,
      // unerased content afterward, not just a stale on-screen glyph.
      // Erasing down to 2+ remaining characters works correctly; it's
      // specifically the "only one character left" case that fails.
      // Detected here by comparing contents before/after actually
      // dispatching the keystroke (rather than pre-emptively guessing
      // from length alone, which can't tell "cursor right after the one
      // character" - where backspace should empty the buffer - apart
      // from "cursor before it" - where backspace should already be a
      // real no-op - since ncreader exposes no public cursor-position
      // query): a Backspace that produces no change at all despite
      // non-empty prior content is always this bug in practice (an
      // honest no-op only happens with an already-empty buffer, excluded
      // by the emptiness check below) - ncreader_clear() (already used
      // the same way by setReaderContents() above) forces the correct
      // empty result directly, bypassing the broken path entirely.
      string before;
      if (input.getId() == NCKEY_BACKSPACE) {
	char * c = ncreader_contents(reader);
	before = c ? c : "";
	free(c);
      }
      auto ni = to_ncinput(input, true); // true case - see to_ncinput()'s own comment
      ncreader_offer_input(reader, &ni);
      if (!before.empty()) {
	char * c = ncreader_contents(reader);
	string after = c ? c : "";
	free(c);
	if (after == before) ncreader_clear(reader);
      }
      return true;
    } else if (selector) {
      auto ni = to_ncinput(input);
      return selector->offer_input(&ni);
    } else {
      return false;
    }
  }
  
  void setOwning(bool t) { owner = t; }

  Plane & getPlane() { return *plane; }

  void refresh() override {
    unsigned int y, x;
    plane->get_dim(&y, &x);
    setDim(pair(static_cast<int>(y), static_cast<int>(x)));
  }

  bool canRenderSextants() const override { return sextant_support_; }

private:
  // Wide enough for " [Sole completion]" (18) - the longer of the two
  // indicator strings showReaderIndicator() ever actually draws - plus a
  // little slack.
  static constexpr unsigned int kIndicatorPlaneCols = 20;

  Plane * plane;
  ncreader * reader = 0;
  // The reader's own left edge, in this plane's (getPlane()'s) own
  // coordinate space - showReader()'s reader_x, remembered so
  // showReaderIndicator() can convert its own "column within the typed
  // text" argument into an absolute column.
  int reader_text_x = 0;
  ncplane * indicator_plane = nullptr;
  unique_ptr<Selector> selector;
  bool owner;
  bool sextant_support_;
};

// One declarative source for every section/item, rather than hand-written
// ncmenu_item/ncmenu_section arrays plus a separately-maintained desc->
// command map (the shape a single File/New item used to get away with) -
// adding an item is one line here, not several coordinated edits.
// `label == nullptr` is a separator (a real ncmenu primitive - NULL desc
// renders a horizontal divider, per notcurses.h's own struct comment - not
// an app-level hack); `binding` is the human-readable keybinding shown
// right-aligned against the item box's widest label, e.g. "Save    C-x
// C-s" - ncmenu_item's own .shortcut field is deliberately left zeroed for
// every item below rather than used for this (see the display-constraints
// note in plans/menu-bar-expansion.md: it renders as a single bare
// character with no modifier indication, misleading for anything but a
// plain unmodified key, which is nearly nothing real this app binds).
// Section headers keep real single-letter Alt+<mnemonic> shortcuts - a
// bare Alt+letter has no such ambiguity.
struct MenuItemSpec {
  const char * label;    // nullptr = separator
  const char * binding;  // human-readable keybinding shown next to label; "" = none
  const char * command;  // name passed to Controller::sendCommand() on activation
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
    { "File", 'f', {
	{ "Open...", "C-x C-f", "open-song" },
	{ "Save", "C-x C-s", "save-song" },
	{ "Save As...", "C-x C-w", "save-song-as" },
	{ nullptr, nullptr, nullptr },
	{ "Quit", "C-x C-c", "save-buffers-kill-terminal" },
      } },
    { "Edit", 'e', {
	{ "Set Mark", "C-SPC", "set-mark" },
	{ "Kill Region", "C-w", "kill-region" },
	{ "Copy", "M-w", "kill-ring-save" },
	{ "Yank", "C-y", "yank" },
	{ "Cancel", "C-g", "keyboard-quit" },
	{ nullptr, nullptr, nullptr },
	{ "Transpose Up", "C-S-Up", "transpose-region-up" },
	{ "Transpose Down", "C-S-Down", "transpose-region-down" },
      } },
    { "Track", 't', {
	{ "Add Instrument Track", "C-t", "add-instrument-track" },
	{ "Add Percussion Track", "C-S-D", "add-percussion-track" },
	{ "Add Sample Track", "C-r", "add-sample-track" },
	{ "Add Group Track", "", "add-group-track" },
	{ "Rename Track...", "F2", "rename-track" },
	{ "Delete Track", "", "delete-track" },
	{ nullptr, nullptr, nullptr },
	{ "Apply Rock Kit", "", "apply-preset-rock" },
	{ "Apply Latin Kit", "", "apply-preset-latin" },
	{ "Apply Electronic Kit", "", "apply-preset-electronic" },
	{ "Remove Kit", "", "apply-preset-none" },
	{ nullptr, nullptr, nullptr },
	{ "Toggle Mute", "\\", "toggle-mute" },
	{ "Toggle Solo", "C-\\", "toggle-solo" },
	{ nullptr, nullptr, nullptr },
	{ "Add Note Column", "C-S-Right", "add-note-column" },
	{ "Remove Note Column", "C-S-Left", "remove-note-column" },
      } },
    // Pattern/song-structural and transport actions - see
    // plans/menu-bar-expansion.md for why these two share one section
    // (no natural distinct single-letter mnemonic for each) and why plain
    // cursor navigation (move-row-up/-down) has no place here at all.
    { "Song", 's', {
	{ "Play/Stop", "SPC", "toggle-playing" },
	{ nullptr, nullptr, nullptr },
	{ "Toggle Binaural Mixer", "", "toggle-mixer-type" },
	{ nullptr, nullptr, nullptr },
	{ "Set Song Key...", "", "set-song-key" },
	{ "Set Tuning System...", "", "set-song-tuning" },
	{ nullptr, nullptr, nullptr },
	{ "Set Bus Effect A...", "", "set-bus-effect-a" },
	{ "Set Bus Effect B...", "", "set-bus-effect-b" },
      } },
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
  buffer_items.push_back({ nullptr, nullptr, nullptr });
  // Menu-only, no keybinding of their own - open the active song's own
  // Pattern Viewer (PatternEditor), Session View (SessionView), or
  // Outline (OutlineView) aspect, symmetrically (see UI.cpp's own
  // "pattern-viewer"/"session-view"/"outline-view" command comment) - any
  // one can be open independently of the others.
  buffer_items.push_back({ "Open Pattern Viewer", "", "pattern-viewer" });
  buffer_items.push_back({ "Open Session View", "", "session-view" });
  buffer_items.push_back({ "Open Outline", "", "outline-view" });
  spec.push_back({ "Buffers", 'b', std::move(buffer_items) });

  return spec;
}

class TerminalMenu : public UIMenu {
public:
  TerminalMenu(vector<string> buffer_names, vector<string> buffer_display_names, string active_buffer_name)
    : buffer_names_(std::move(buffer_names)), buffer_display_names_(std::move(buffer_display_names)),
      active_buffer_name_(std::move(active_buffer_name)) { rebuild(); }

  void refreshBuffers(const vector<string> & buffer_names, const vector<string> & buffer_display_names,
		       const string & active_buffer_name) override {
    buffer_names_ = buffer_names;
    buffer_display_names_ = buffer_display_names;
    active_buffer_name_ = active_buffer_name;
    rebuild();
  }

  // Confirmed against the real library: ncmenu_offer_input() never treats a
  // click on an item (as opposed to a section header) or an Enter keypress
  // as "activating" that item, on its own - both are absent from its own
  // documented list of inputs it reacts to. It also doesn't need to (there's
  // no notion of a command to run baked into an ncmenu_item, just display
  // text), so this app has to detect activation itself: a button-release
  // landing on an item (ncmenu_mouse_selected(), checked before
  // offer_input() would otherwise just silently ignore that same click -
  // it's not "outside" the plane, which has grown to cover the dropdown, so
  // offer_input() doesn't roll up on it either) or Enter while some item is
  // highlighted (ncmenu_selected() non-null), then map the item's display
  // text to a command name and roll the section back up.
  bool offerInput(const InputEvent & input) override {
    auto ni = to_ncinput(input);

    if (ni.id == NCKEY_BUTTON1 && ni.evtype == NCTYPE_RELEASE) {
      ncinput shortcut_ni;
      if (auto clicked = menu->get_mouse_selected(&ni, &shortcut_ni)) {
	activate(clicked);
	return true;
      }
    }

    // Checked before offer_input() gets a chance to ignore it (returning
    // false, per the confirmed absence of Enter from its own list) and
    // leak the keystroke through to the pattern editor as a note.
    if (ni.id == NCKEY_ENTER) {
      if (auto sel = menu->get_selected()) {
	activate(sel);
	return true;
      }
    }

    return menu->offer_input(&ni);
  }

  std::string takeActivatedCommand() override {
    string c = std::move(activated_command_);
    activated_command_.clear();
    return c;
  }

  void raiseToTop() override { menu->get_plane()->move_top(); }

private:
  void activate(const char * item_desc) {
    if (auto it = item_commands_.find(item_desc); it != item_commands_.end()) activated_command_ = it->second;
    menu->rollup();
  }

  // (Re)builds the whole ncmenu from scratch against buffer_names_' current
  // contents - the constructor's original one-shot job, now also
  // refreshBuffers()'s, since ncmenu has no API to replace one section's
  // items in an existing menu (confirmed against the real library: nothing
  // between ncmenu_create() and ncmenu_destroy() touches item content) -
  // recreating the whole `menu` is the only way to show a changed buffer
  // list. The old Menu (and its ncplane) is destroyed by the reassignment
  // below; the render loop's own per-frame raiseToTop() (TerminalUI's main
  // loop) picks the replacement back up without this needing to call it
  // itself.
  void rebuild() {
    // desc_storage_/item_commands_ back every non-separator ncmenu_item's
    // .desc pointer and the activation lookup below respectively - reset
    // together with menu itself so a rebuild never leaves either holding a
    // stale entry from the previous buffer list.
    desc_storage_.clear();
    item_commands_.clear();

    // Every buffer item's *label* is buffer_display_names_' own text
    // (Controller::getBufferDisplayName()'s already-disambiguated output,
    // computed by the caller - see refreshBuffers()'s own comment on
    // UIMenu.h for why this class can't just call it itself), not the
    // full path buffer_names_ itself carries (which stays the real songs_
    // key everywhere else, e.g. save-song's target) - prefixed with "* "
    // for whichever one is active_buffer_name_. Each item's own *command*
    // is still "switch-to-buffer:" plus the *full* name
    // (Controller::refreshBufferCommands() keeps one such command defined
    // per open buffer), never the display text - a menu click has to
    // reach the same specific buffer a same-display one wouldn't.
    // display_labels/commands are this function's own local vectors, not
    // menuSpec()'s: MenuItemSpec::label/command point directly into
    // whatever backs them without copying, so both must outlive
    // menuSpec()'s own return - true for locals here (alive for the rest
    // of this function), not for ones that would go out of scope the
    // moment menuSpec() itself returned.
    vector<string> display_labels, commands;
    display_labels.reserve(buffer_names_.size());
    commands.reserve(buffer_names_.size());
    for (size_t i = 0; i < buffer_names_.size(); i++) {
      auto & name = buffer_names_[i];
      auto & display_name = i < buffer_display_names_.size() ? buffer_display_names_[i] : name;
      display_labels.push_back((name == active_buffer_name_ ? "* " : "  ") + display_name);
      commands.push_back("switch-to-buffer:" + name);
    }
    vector<MenuItemSpec> buffer_items;
    buffer_items.reserve(buffer_names_.size());
    for (size_t i = 0; i < buffer_names_.size(); i++) {
      buffer_items.push_back({ display_labels[i].c_str(), "", commands[i].c_str() });
    }

    auto spec = menuSpec(std::move(buffer_items));

    // desc_storage_ is reserved to its final size up front so no
    // reallocation (which would move/invalidate short (SSO) strings' own
    // buffers) can happen while still being filled below; must stay alive
    // through ncmenu_create()'s call at the bottom of this function, which
    // deep-copies the ncmenu_item/ncmenu_section arrays themselves
    // (confirmed against the real library - see
    // plans/menu-bar-expansion.md) but not anything *they* point to a
    // moment later.
    size_t total_items = 0;
    for (auto & section : spec) total_items += section.items.size();
    desc_storage_.reserve(total_items);

    vector<vector<ncmenu_item>> item_arrays;
    vector<ncmenu_section> sections;
    for (auto & section : spec) {
      size_t label_width = 0;
      for (auto & item : section.items) {
	if (item.label) label_width = std::max(label_width, strlen(item.label));
      }

      vector<ncmenu_item> items;
      for (auto & item : section.items) {
	if (!item.label) {
	  items.push_back({ .desc = nullptr, .shortcut = {} });
	  continue;
	}
	string desc = item.label;
	if (item.binding && item.binding[0]) {
	  desc.append(label_width + 2 - strlen(item.label), ' ');
	  desc += item.binding;
	}
	desc_storage_.push_back(std::move(desc));
	items.push_back({ .desc = desc_storage_.back().c_str(), .shortcut = {} });
	if (item.command) item_commands_[desc_storage_.back()] = item.command;
      }
      item_arrays.push_back(std::move(items));
    }
    for (size_t i = 0; i < item_arrays.size(); i++) {
      auto & section = spec[i];
      sections.push_back({ .name = section.name, .itemcount = static_cast<int>(item_arrays[i].size()),
	.items = item_arrays[i].data(),
	.shortcut = { .id = static_cast<uint32_t>(section.mnemonic), .alt = true } });
    }

    uint64_t headerchannels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0x7f, 0x34, 0x7f);
    uint64_t sectionchannels = NCCHANNELS_INITIALIZER(0xff, 0xff, 0xff, 0x00, 0x00, 0x00);
    ncchannels_set_fg_alpha(&sectionchannels, NCALPHA_HIGHCONTRAST);
    ncchannels_set_bg_alpha(&sectionchannels, NCALPHA_BLEND);
    ncchannels_set_bg_alpha(&headerchannels, NCALPHA_BLEND);
    ncmenu_options mopts = { .sections = sections.data(), .sectioncount = static_cast<int>(sections.size()),
      .headerchannels = headerchannels, .sectionchannels = sectionchannels, .flags = 0 };
    menu = make_unique<Menu>(&mopts);
  }

  vector<string> buffer_names_;
  vector<string> buffer_display_names_;
  string active_buffer_name_;
  unique_ptr<Menu> menu;
  vector<string> desc_storage_;
  std::map<string, string> item_commands_;
  string activated_command_;
};

class TerminalChart : public Chart {
public:
  TerminalChart(UIPlane & parent, ChartType type, double min_y = 0.0, double max_y = 0.0) : Chart(parent, type, min_y, max_y) {
    // Same window_bg_color base-cell fix as plot_plane_ gets below, but
    // for this widget's own outer plane - the footer label row is
    // deliberately left uncovered by plot_plane_ so its text shows
    // through, and any of that row's cells the label text itself doesn't
    // reach (getMeterLabel() rarely fills the whole width) need this too.
    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    uint64_t base_channels = NCCHANNELS_INITIALIZER(21, 21, 21, 21, 21, 21);
    ncplane_set_base(tplane.getPlane().to_ncplane(), " ", 0, base_channels);
  }

  void setSample(int i, double v) override {
    if (!plot_) {
      // ncdplot_create()/ncdplot_destroy() take ownership of the ncplane
      // passed in and destroy it together with the plot (confirmed
      // empirically: resizing the plane after destroying the plot
      // segfaults). Since this chart's own plane (getPlane()) must survive
      // resizes for the chart's whole lifetime, give the plot a dedicated,
      // disposable child plane instead of handing away our own.
      auto [rows, cols] = getDim();
      auto [y, x] = getPosition();
      plot_plane_ = getPlane().createChild();
      // createChild()'s underlying Plane ctor has no parent-plane argument -
      // it places the new plane at (0,0) in the standard plane's coordinate
      // space, not relative to our own (already correctly positioned)
      // plane. Reposition it explicitly to match, or it always ends up at
      // whatever raw (0,0) createChild() hardcodes regardless of where this
      // chart actually is on screen.
      // When a footer label is set, the plot only gets rows-1 - it's a
      // child plane, so leaving the last row of our own (outer) plane_
      // uncovered is what lets that row's putstr() in commit() actually
      // show through, rather than being hidden behind the plot child.
      int plot_rows = footer_label_.empty() ? rows : rows - 1;
      if (plot_rows <= 0) plot_rows = rows;
      plot_plane_->resize(plot_rows, cols);
      plot_plane_->move(y, x);

      auto & tplane = dynamic_cast<TerminalPlane&>(*plot_plane_);
      tplane.setOwning(false);

      // Set the plane's *base* cell to the same idle background every
      // scope now shares (StyleProvider::window_bg_color "#151515",
      // HeatmapChart's own kHeatmapBackground) - this, not a plain
      // putstr()-based fill (tried and reverted here), is what actually
      // survives ncdplot's own rendering: ncdplot_create()/every
      // subsequent redraw calls ncplane_erase() internally on its own
      // plane before repainting the "lit" portion of each column, and
      // per notcurses's own contract "the base cell is not affected by
      // ncplane_erase()" - it's what shows through any cell whose real
      // content is still blank (gcluster 0) after that erase. A plain
      // fill got wiped by that same erase every render, leaving a freshly
      // created plane's own default (the raw terminal's background, not
      // this app's) showing through the "no data" portion of the chart -
      // a real, confirmed bug.
      uint64_t base_channels = NCCHANNELS_INITIALIZER(21, 21, 21, 21, 21, 21);
      ncplane_set_base(tplane.getPlane().to_ncplane(), " ", 0, base_channels);

      ncplot_options opts;
      memset(&opts, 0, sizeof(opts));
      opts.flags = 0
	// | NCPLOT_OPTION_LABELTICKSD
	// | NCPLOT_OPTION_EXPONENTIALD
	// | NCPLOT_OPTION_PRINTSAMPLE
	;
      opts.gridtype = getType() == DOTS ? NCBLIT_BRAILLE : NCBLIT_2x2;
      // opts.gridtype = NCBLIT_8x1;

      // Opaque window_bg_color ("#151515") background, not NCALPHA_BLEND -
      // blend mode composites against whatever's on the plane beneath at
      // render time, which for a cell ncdplot actually writes to isn't
      // reliably this app's own background (a real, confirmed bug: every
      // rendered dot showed the raw terminal's own background bleeding
      // through, even though ncplane_set_base() above already fixed the
      // *untouched* cells around them). Foreground gradient (dot color)
      // is unchanged - only the background channel/alpha needed fixing.
      opts.minchannels = NCCHANNELS_INITIALIZER(0x80, 0x80, 0xff, 21, 21, 21);
      opts.maxchannels = NCCHANNELS_INITIALIZER(0x80, 0xff, 0x80, 21, 21, 21);

      plot_ = std::make_shared<PlotD>(tplane.getPlane(), &opts);
    }

    plot_->set_sample(static_cast<uint64_t>(i), v);
  }

  void commit() override {
    if (!footer_label_.empty()) {
      auto [rows, cols] = getDim();
      // window_fg_color/window_bg_color ("#9e9e9e"/"#151515") - this
      // widget has no StyleProvider reference of its own (only
      // UI-level/render-time callers normally do), so these are the same
      // literal RGB values kHeatmapBackground uses above for the same
      // reason. Without this, the label text drew with whatever fg/bg the
      // outer plane's draw state last happened to be left in - unset in
      // practice, showing the raw terminal's own background instead of
      // this app's (the outer-plane counterpart of the ncplot fix above -
      // that one only covers plot_plane_, not this row, which is
      // deliberately left uncovered by it so this text shows through).
      setFgColor(0x9e, 0x9e, 0x9e);
      setBgColor(21, 21, 21);
      putstr(rows - 1, 0, footer_label_);
    }
  }

protected:
  void onResize() override {
    plot_.reset();       // destroys the ncdplot, which destroys plot_plane_'s ncplane too
    plot_plane_.reset();  // drop our now-hollow wrapper (owner=false, so no double-free)
    // next setSample() lazily rebuilds both against the new dimensions
  }

private:
  std::shared_ptr<PlotD> plot_;
  std::unique_ptr<UIPlane> plot_plane_;
};

// Renders via notcurses's ncvisual/pixel-graphics subsystem (sixel/kitty-
// graphics/iTerm2, whichever the terminal supports) instead of ncplot's
// braille/block glyphs, for much higher effective resolution. Buffers
// samples cheaply per setSample() call and does the actual RGBA-build-and-
// blit work once per commit(), directly onto this chart's own plane (no
// widget/plane-ownership landmine like TerminalChart's ncplot - ncvisual
// blitting draws onto an existing plane, it doesn't adopt/destroy it).
class TerminalPixelChart : public Chart {
public:
  TerminalPixelChart(UIPlane & parent, ChartType type, double min_y = 0.0, double max_y = 0.0) : Chart(parent, type, min_y, max_y) { }

  void setSample(int i, double v) override {
    if (i >= static_cast<int>(samples_.size())) samples_.resize(static_cast<size_t>(i + 1));
    samples_[static_cast<size_t>(i)] = v;
  }

  void commit() override {
    if (samples_.empty()) return;

    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    auto native_plane = tplane.getPlane().to_ncplane();

    unsigned pxy = 0, pxx = 0, celldimy = 0;
    ncplane_pixel_geom(native_plane, &pxy, &pxx, &celldimy, nullptr, nullptr, nullptr);
    if (pxy == 0 || pxx == 0) return;

    // Reserve exactly one character row's worth of pixels at the bottom for
    // the footer label (see Chart::setFooterLabel), so the bar image itself
    // never gets drawn under/behind the text - pxy is always an exact
    // multiple of celldimy per ncplane_pixel_geom's own contract, so this
    // shrinks the image by exactly one whole cell row, not a partial one.
    if (!footer_label_.empty() && celldimy > 0 && pxy > celldimy) pxy -= celldimy;

    // Opaque window_bg_color ("#151515"), not transparent (0 alpha) - a
    // transparent pixel here composites against the raw terminal's own
    // background instead of this app's, since pixel-graphics blitting
    // replaces a cell's usual text-mode background entirely rather than
    // layering over whatever this plane's cells were otherwise painted
    // (a real, confirmed bug: this chart's empty area showed Ubuntu's
    // default terminal color instead of window_bg_color).
    vector<uint32_t> buffer(static_cast<size_t>(pxy) * pxx, 0xff151515u);

    auto range = max_y_ - min_y_;
    auto num_samples = samples_.size();
    for (unsigned x = 0; x < pxx; x++) {
      auto sample_idx = min(static_cast<size_t>(x) * num_samples / pxx, num_samples - 1);
      auto v = samples_[sample_idx];
      auto frac = range > 0 ? (v - min_y_) / range : 0.0;
      if (frac < 0) frac = 0;
      else if (frac > 1) frac = 1;
      auto bar_height = static_cast<unsigned>(frac * pxy);

      for (unsigned y = 0; y < bar_height; y++) {
	// dim blue-ish at the bottom (quiet) to green at the top (loud),
	// matching TerminalChart's existing min/max channel colors.
	double t = pxy > 1 ? static_cast<double>(y) / (pxy - 1) : 0.0;
	uint8_t r = static_cast<uint8_t>(0x80);
	uint8_t g = static_cast<uint8_t>(0x80 * (1 - t) + 0xff * t);
	uint8_t b = static_cast<uint8_t>(0xff * (1 - t) + 0x80 * t);
	unsigned py = pxy - 1 - y; // bars grow upward from the bottom
	buffer[py * pxx + x] = (0xffu << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
      }
    }

    ncpp::Visual visual(buffer.data(), static_cast<int>(pxy), static_cast<int>(pxx * 4), static_cast<int>(pxx));
    ncvisual_options vopts{};
    vopts.n = native_plane;
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;
    visual.blit(&vopts);

    if (!footer_label_.empty()) {
      auto [rows, cols] = getDim();
      putstr(rows - 1, 0, footer_label_);
    }
  }

private:
  std::vector<double> samples_;
};

namespace {

// StyleProvider::window_bg_color ("#151515") - the same idle background
// every scope now shares (the FFT/volume-meter charts already show it
// through untouched cells; the heatmap has no such "untouched" concept
// since it repaints every cell/pixel every frame, so it needs its own
// explicit idle color instead of literal black to match).
constexpr SubcellRgb kHeatmapBackground{21.0f, 21.0f, 21.0f};

// Lerps from kHeatmapBackground (value == 0) to the fully-bright HSV color
// (value == 1), using value itself as the blend weight - continuous by
// construction, so a cell whose value only ever asymptotically approaches
// 0 (dsp/DiracAnalyzer.cpp's grid ballistics are a one-pole decay that
// never mathematically reaches exact 0) still converges to the background
// color rather than needing a separate cutoff/threshold to special-case
// "close enough to silent." No explicit epsilon check needed anywhere:
// once value is astronomically small, its weight in the lerp is too.
SubcellRgb heatmapCellColor(float saturation, float value) {
  if (value < 0.0f) value = 0.0f;
  else if (value > 1.0f) value = 1.0f;
  uint8_t r, g, b;
  heatmapHsvToRgb(kHeatmapHue, saturation, 1.0f, r, g, b);
  return {
    kHeatmapBackground.r * (1.0f - value) + static_cast<float>(r) * value,
    kHeatmapBackground.g * (1.0f - value) + static_cast<float>(g) * value,
    kHeatmapBackground.b * (1.0f - value) + static_cast<float>(b) * value,
  };
}

// Computes destination index d's source-index range [lo,hi) along one axis
// of size grid_size, resampled to dest_size - two different, deliberately
// separate strategies depending on direction, because a single formula
// that's correct for one gets the other wrong:
//
// - Downsampling (dest_size <= grid_size, TerminalHeatmapChart's usual
//   case): a proper partition via round(d*grid_size/dest_size) boundaries,
//   not floor or ceiling. lo(d) is literally hi(d-1), so consecutive
//   buckets never leave a gap (a truncating/floor upper bound can drop a
//   source cell a bucket's real span only partially reaches into - a real,
//   confirmed bug: grid_size=18, dest_size=15, d=7's real span [8.4,9.6)
//   genuinely overlaps both source rows 8 and 9, but floor(9.6)=9 as an
//   exclusive bound only ever included row 8) nor overlap (a ceiling upper
//   bound independently applied to every d, tried and reverted here, fixed
//   the gap but then had d=6's range reach into row 8 too, so an
//   elevation=0 source split 50/50 across rows 8/9 by DiracAnalyzer.cpp's
//   own bilinear splat lit up all 3 sextant sub-rows instead of
//   concentrating in the true middle one).
//
// - Upsampling (dest_size > grid_size, TerminalPixelHeatmapChart's usual
//   case, and TerminalHeatmapChart's own azimuth axis before
//   DiracAnalyzer::kAzimuthBins was widened enough to avoid it): the same
//   round-boundary formula can tie at consecutive d (two different d's
//   round to the same source index), leaving one of them an empty range;
//   patching that by grabbing the *next* source index (as the downsampling
//   fallback below does) breaks the tie asymmetrically, double-covering
//   one source cell while its symmetric neighbor only gets single
//   coverage - the horizontal counterpart of the elevation bug above (a
//   source split 50/50 across two adjacent azimuth bins rendered as three
//   equally-bright sub-columns instead of a symmetric two-and-two split).
//   Each destination index instead independently gets its own single
//   nearest source cell, by that destination cell's real-valued *center*
//   (d+0.5) rather than its edge - ties are then impossible since
//   consecutive d's centers can never round to the same source index the
//   way consecutive *boundaries* can.
void axisRange(int d, int grid_size, int dest_size, int & lo, int & hi) {
  if (dest_size <= grid_size) {
    lo = (d * grid_size + dest_size / 2) / dest_size;
    hi = ((d + 1) * grid_size + dest_size / 2) / dest_size;
    if (hi <= lo) hi = lo + 1; // degenerate (dest_size far smaller than grid_size): still show something
  } else {
    lo = ((2 * d + 1) * grid_size) / (2 * dest_size); // floor((d+0.5)*grid_size/dest_size)
    hi = lo + 1;
  }
  if (lo >= grid_size) lo = grid_size - 1;
  if (hi > grid_size) hi = grid_size;
}

// Resamples the logical grid_cols x grid_rows brightness/saturation grid
// to a dest_cols x dest_rows destination resolution, correct whether the
// destination is coarser (TerminalHeatmapChart's usual case, a handful of
// quadrant sub-cells) or finer (TerminalPixelHeatmapChart's usual case,
// real display pixels) than the logical grid: each destination cell
// aggregates (max, keeping saturation paired with whichever source cell
// "won" the max rather than maximized independently) over every source
// cell axisRange() (above) assigns it. Both dest_row 0 and grid row 0 are
// "bottom" here - callers flip to screen coordinates (row 0 = top)
// themselves.
void resampleGrid(const std::vector<float> & brightness, const std::vector<float> & saturation,
                   int grid_cols, int grid_rows, int dest_cols, int dest_rows,
                   std::vector<float> & out_brightness, std::vector<float> & out_saturation) {
  out_brightness.assign(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows), 0.0f);
  out_saturation.assign(static_cast<size_t>(dest_cols) * static_cast<size_t>(dest_rows), 0.0f);
  if (dest_cols <= 0 || dest_rows <= 0) return;

  for (int dy = 0; dy < dest_rows; dy++) {
    int gy0, gy1;
    axisRange(dy, grid_rows, dest_rows, gy0, gy1);
    for (int dx = 0; dx < dest_cols; dx++) {
      int gx0, gx1;
      axisRange(dx, grid_cols, dest_cols, gx0, gx1);

      float best_brightness = 0.0f, best_saturation = 0.0f;
      for (int gy = gy0; gy < gy1; gy++) {
        for (int gx = gx0; gx < gx1; gx++) {
          size_t src_idx = static_cast<size_t>(gy * grid_cols + gx);
          if (brightness[src_idx] > best_brightness) {
            best_brightness = brightness[src_idx];
            best_saturation = saturation[src_idx];
          }
        }
      }
      out_brightness[static_cast<size_t>(dy * dest_cols + dx)] = best_brightness;
      out_saturation[static_cast<size_t>(dy * dest_cols + dx)] = best_saturation;
    }
  }
}

struct AxisLabel { int row, col; const char * text; };

// The heatmap's own axis extremes, placed directly in the grid area rather
// than a separate legend row - elevation labels on the top/bottom rows
// (matching HeatmapChart's row-0-is-bottom-of-data/screen-row-0-is-top
// convention, so "+90" naturally sits where the top of the grid is),
// azimuth labels at the left/right edges of a middle row (azimuth bin 0
// is the left edge, bin kAzimuthBins-1 the right - see DiracAnalyzer.cpp's
// own az_pos splat math). Labels are always drawn, never hidden by
// activity underneath them - instead, each character cell a label
// occupies is an exception to the normal two-color quadrant/pixel
// blend: it collapses to a single mean color (its underlying sub-samples
// averaged) used as that character's background, with the glyph itself
// drawn in white blended with transparency over that background
// (labelForegroundColor(), below) - so the label always reads clearly
// while still visibly taking on whatever hue/brightness is really there.
constexpr float kLabelForegroundAlpha = 0.55f; // how much white shows through the glyph

void labelForegroundColor(const SubcellRgb & bg, uint8_t & r, uint8_t & g, uint8_t & b) {
  r = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.r);
  g = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.g);
  b = static_cast<uint8_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * bg.b);
}

std::array<AxisLabel, 4> axisLabels(int usable_rows, int cols) {
  int el_col = max(0, (cols - 3) / 2); // "+90"/"-90" are both 3 characters wide
  return {{
    { 0, el_col, "+90" },
    { usable_rows - 1, el_col, "-90" },
    { usable_rows / 2, 0, "-180" },
    { usable_rows / 2, cols - 4, "+180" },
  }};
}
}

// Character-cell fallback for HeatmapChart, no pixel-graphics support
// needed: paints one Unicode block glyph per character cell - a sextant
// (2x3 sub-cells, SubcellGlyphs.h's sextantCodepoint()) when
// UIPlane::canRenderSextants() confirms the terminal supports Unicode
// 13's sextant range, else a quadrant (2x2 sub-cells, kQuadrantCodepoints),
// read once at construction (the terminal's capabilities don't change
// mid-session) - tripling (sextants) or doubling (quadrants) the
// effective vertical resolution over a flat one-color-per-cell approach.
// Each cell's foreground/background pair is the optimal 2-color
// quantization (quantizeToTwoColors) of its real sub-samples, not a naive
// corner pick or a fixed brightness threshold. 24-bit truecolor is
// near-universally supported even on terminals without sixel/Kitty image
// support, unlike the braille dot glyphs TerminalChart uses for 1D bars -
// a heatmap wants a genuine 2D color field, not a thresholded on/off
// pattern along one axis. Resamples (resampleGrid(), above) from the
// logical gridCols()xgridRows() grid to however many sub-cells this
// widget actually has (2x the character-cell count horizontally always,
// 3x or 2x vertically depending on sextant support).
class TerminalHeatmapChart : public HeatmapChart {
public:
  TerminalHeatmapChart(UIPlane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) {
    use_sextants_ = getPlane().canRenderSextants();
  }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void commit() override {
    if (brightness_.empty()) return;
    auto [rows, cols] = getDim();
    int usable_rows = footer_label_.empty() ? rows : rows - 1;
    if (usable_rows <= 0 || cols <= 0) return;

    int sub_rows = use_sextants_ ? 3 : 2;
    int vcols = cols * 2, vrows = usable_rows * sub_rows; // sextant/quadrant sub-cell resolution
    std::vector<float> agg_brightness, agg_saturation;
    resampleGrid(brightness_, saturation_, gridCols(), gridRows(), vcols, vrows, agg_brightness, agg_saturation);

    std::vector<SubcellRgb> samples(static_cast<size_t>(sub_rows * 2));
    for (int sy = 0; sy < usable_rows; sy++) {
      for (int sx = 0; sx < cols; sx++) {
        // Gather this character cell's sub-samples (row-major - top-left
        // first, matching kQuadrantCodepoints'/sextantCodepoint()'s own bit
        // order) from the resampled sub-grid. Screen row 0 is the TOP of
        // the widget, but sub-grid row 0 is the BOTTOM (resampleGrid()'s
        // contract, inherited from HeatmapChart.h's own setGrid()) - flip
        // vertically.
        int bit = 0;
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = sy * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            samples[static_cast<size_t>(bit)] = heatmapCellColor(agg_saturation[idx], agg_brightness[idx]);
            bit++;
          }
        }

        SubcellRgb on_color, off_color;
        int mask = quantizeToTwoColors(samples, on_color, off_color);

        setFgColor(static_cast<int>(on_color.r), static_cast<int>(on_color.g), static_cast<int>(on_color.b));
        setBgColor(static_cast<int>(off_color.r), static_cast<int>(off_color.g), static_cast<int>(off_color.b));
        putstr(sy, sx, Utf8::encodeCodepoint(use_sextants_ ? sextantCodepoint(mask) : kQuadrantCodepoints[mask]));
      }
    }
    setFgColor(255, 255, 255);
    setBgColor(static_cast<int>(kHeatmapBackground.r), static_cast<int>(kHeatmapBackground.g), static_cast<int>(kHeatmapBackground.b)); // don't leave the last cell's colors "stuck" for whatever draws next (e.g. the footer label below)

    for (auto & marker : markers_) {
      int sx = static_cast<int>(marker.u * static_cast<float>(cols));
      int sy = usable_rows - 1 - static_cast<int>(marker.v * static_cast<float>(usable_rows));
      if (sx < 0 || sx >= cols || sy < 0 || sy >= usable_rows) continue;
      putstr(sy, sx, '+');
    }

    for (auto & label : axisLabels(usable_rows, cols)) {
      std::string text = label.text;
      for (int c = 0; c < static_cast<int>(text.size()); c++) {
        int sx = label.col + c;
        if (sx < 0 || sx >= cols) continue;

        // This character's own sub-samples, collapsed to a single mean
        // color - the exception to the normal two-color sextant/quadrant
        // blend that a label-covered cell gets (see axisLabels()'s own
        // comment above).
        SubcellRgb sum{0, 0, 0};
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = label.row * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            SubcellRgb rgb = heatmapCellColor(agg_saturation[idx], agg_brightness[idx]);
            sum.r += rgb.r; sum.g += rgb.g; sum.b += rgb.b;
          }
        }
        float sample_count = static_cast<float>(sub_rows * 2);
        SubcellRgb mean{sum.r / sample_count, sum.g / sample_count, sum.b / sample_count};

        uint8_t fr, fg, fb;
        labelForegroundColor(mean, fr, fg, fb);
        setFgColor(fr, fg, fb);
        setBgColor(static_cast<int>(mean.r), static_cast<int>(mean.g), static_cast<int>(mean.b));
        putstr(label.row, sx, std::string(1, text[static_cast<size_t>(c)]));
      }
    }

    if (!footer_label_.empty()) {
      putstr(rows - 1, 0, footer_label_);
    }
  }

private:
  bool use_sextants_;
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
};

// Pixel-graphics HeatmapChart renderer - same ncvisual/pixel-blit approach
// as TerminalPixelChart, but a genuine 2D image resampled (resampleGrid(),
// above - typically upscaling here, since real pixel counts usually exceed
// the logical grid's 36x18) to the plane's real pixel dimensions, instead
// of vertical bars.
class TerminalPixelHeatmapChart : public HeatmapChart {
public:
  TerminalPixelHeatmapChart(UIPlane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) { }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void commit() override {
    if (brightness_.empty()) return;

    auto & tplane = dynamic_cast<TerminalPlane&>(getPlane());
    auto native_plane = tplane.getPlane().to_ncplane();

    unsigned pxy = 0, pxx = 0, celldimy = 0, celldimx = 0;
    ncplane_pixel_geom(native_plane, &pxy, &pxx, &celldimy, &celldimx, nullptr, nullptr);
    if (pxy == 0 || pxx == 0) return;
    if (celldimy == 0) celldimy = 1;
    if (celldimx == 0) celldimx = 1;

    // Same footer-row reservation as TerminalPixelChart::commit().
    if (!footer_label_.empty() && celldimy > 0 && pxy > celldimy) pxy -= celldimy;

    vector<uint32_t> buffer(static_cast<size_t>(pxy) * pxx, 0xff151515u); // opaque kHeatmapBackground - overwritten below for every pixel regardless, kept consistent for clarity

    std::vector<float> agg_brightness, agg_saturation;
    resampleGrid(brightness_, saturation_, gridCols(), gridRows(), static_cast<int>(pxx), static_cast<int>(pxy), agg_brightness, agg_saturation);

    for (unsigned py = 0; py < pxy; py++) {
      // agg_* row 0 is the BOTTOM (resampleGrid()'s contract), but py=0 is
      // the TOP of the image - flip vertically.
      unsigned vy = pxy - 1 - py;
      for (unsigned px = 0; px < pxx; px++) {
        size_t idx = static_cast<size_t>(vy) * pxx + px;

        SubcellRgb rgb = heatmapCellColor(agg_saturation[idx], agg_brightness[idx]);
        buffer[py * pxx + px] = (0xffu << 24) | (static_cast<uint32_t>(rgb.b) << 16) | (static_cast<uint32_t>(rgb.g) << 8) | static_cast<uint32_t>(rgb.r);
      }
    }

    for (auto & marker : markers_) {
      unsigned mx = static_cast<unsigned>(marker.u * static_cast<float>(pxx));
      unsigned my_from_bottom = static_cast<unsigned>(marker.v * static_cast<float>(pxy));
      unsigned my = my_from_bottom < pxy ? pxy - 1 - my_from_bottom : 0;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          long y = static_cast<long>(my) + dy, x = static_cast<long>(mx) + dx;
          if (y < 0 || y >= static_cast<long>(pxy) || x < 0 || x >= static_cast<long>(pxx)) continue;
          buffer[static_cast<size_t>(y) * pxx + static_cast<size_t>(x)] = 0xffffffffu; // opaque white
        }
      }
    }

    ncpp::Visual visual(buffer.data(), static_cast<int>(pxy), static_cast<int>(pxx * 4), static_cast<int>(pxx));
    ncvisual_options vopts{};
    vopts.n = native_plane;
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;
    visual.blit(&vopts);

    // Axis labels overlay the already-blitted image (same "putstr after
    // blit still shows through" the footer label below already relies on)
    // - always drawn (see axisLabels()'s own comment): each label
    // character's pixel footprint is collapsed to a single mean color used
    // as its background, with the glyph itself in white-blended-with-
    // transparency (labelForegroundColor()) over that background.
    auto [rows, cols] = getDim();
    int usable_rows = footer_label_.empty() ? static_cast<int>(rows) : static_cast<int>(rows) - 1;
    for (auto & label : axisLabels(usable_rows, static_cast<int>(cols))) {
      std::string text = label.text;
      for (int c = 0; c < static_cast<int>(text.size()); c++) {
        int col = label.col + c;
        if (col < 0 || col >= static_cast<int>(cols)) continue;
        unsigned px0 = static_cast<unsigned>(col) * celldimx;
        unsigned px1 = min(pxx, px0 + celldimx);
        unsigned py0 = static_cast<unsigned>(label.row) * celldimy;
        unsigned py1 = min(pxy, py0 + celldimy);

        double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
        unsigned count = 0;
        for (unsigned py = py0; py < py1; py++) {
          for (unsigned px = px0; px < px1; px++) {
            uint32_t pixel = buffer[py * pxx + px];
            sum_r += static_cast<float>(pixel & 0xffu);
            sum_g += static_cast<float>((pixel >> 8) & 0xffu);
            sum_b += static_cast<float>((pixel >> 16) & 0xffu);
            count++;
          }
        }
        if (count == 0) continue;
        SubcellRgb mean{static_cast<float>(sum_r / count), static_cast<float>(sum_g / count), static_cast<float>(sum_b / count)};

        uint8_t fr, fg, fb;
        labelForegroundColor(mean, fr, fg, fb);
        setFgColor(fr, fg, fb);
        setBgColor(static_cast<int>(mean.r), static_cast<int>(mean.g), static_cast<int>(mean.b));
        putstr(label.row, col, std::string(1, text[static_cast<size_t>(c)]));
      }
    }

    if (!footer_label_.empty()) {
      putstr(static_cast<int>(rows) - 1, 0, footer_label_);
    }
  }

private:
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
};

// How long a pending Escape (EscapeSequenceCoalescer::escapePending())
// must stay open before the "ESC-" indicator actually appears
// (updateEscapeIndicator()) - long enough that an ordinary fast Alt-chord
// (typed close enough together to resolve within one readInput() drain
// burst, or split across two but still well within human reaction time)
// never flickers it, short enough that a person who actually paused after
// Escape gets confirmation promptly rather than wondering whether the
// keypress registered at all.
constexpr auto kEscapeIndicatorDelay = std::chrono::milliseconds(300);

// Bundles the Alt-key coalescing pipeline (see EscapeCoalescer.h)'s three
// pieces - defined here, not in TerminalUI.h, so that header stays free
// of notcurses types (same reasoning as kp_escape_pending_'s own comment
// there).
class TerminalUI::EscapeInputPipeline {
public:
  explicit EscapeInputPipeline(ncpp::NotCurses & nc) : source(nc), coalescer(source, detector) { }

  KittyProtocolDetector detector;
  NotcursesInputEventSource source;
  EscapeSequenceCoalescer coalescer;
};

TerminalUI::TerminalUI(std::shared_ptr<ncpp::NotCurses> _nc)
  : nc(_nc), escape_input_(make_unique<EscapeInputPipeline>(*nc)) {
}

TerminalUI::~TerminalUI() {
}

// Called once per startUI() loop iteration, whether it woke on a real fd
// or the poll() timeout below - shows "ESC-" the moment kEscapeIndicatorDelay
// has actually elapsed with the wait still open. Clearing it again happens
// in readInput() instead, right when the wait ends (immediately, not on a
// delay - there's nothing to debounce about a wait that's already over).
void
TerminalUI::updateEscapeIndicator() {
  if (!escape_pending_since_ || escape_indicator_shown_) return;
  if (std::chrono::steady_clock::now() - *escape_pending_since_ >= kEscapeIndicatorDelay) {
    setStatus("ESC-");
    escape_indicator_shown_ = true;
  }
}

// startUI()'s own poll() otherwise waits up to a full second regardless of
// what's happening - fine normally, but while a pending Escape hasn't yet
// crossed kEscapeIndicatorDelay, that would make the indicator's own
// appearance late and irregular (anywhere up to a second past the actual
// delay) rather than landing right on it. Only shortens the wait while
// there's an actual reason to wake up early; otherwise unchanged.
int
TerminalUI::escapeIndicatorPollTimeoutMs() const {
  constexpr int kDefaultPollTimeoutMs = 1000;
  if (!escape_pending_since_ || escape_indicator_shown_) return kDefaultPollTimeoutMs;
  auto remaining = kEscapeIndicatorDelay - (std::chrono::steady_clock::now() - *escape_pending_since_);
  auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
  return static_cast<int>(std::clamp<long long>(remaining_ms, 0, kDefaultPollTimeoutMs));
}

void
TerminalUI::initialize(std::shared_ptr<Controller> & controller) {
  auto root_plane = make_unique<TerminalPlane>(controller, nc->get_stdplane(), false);
  setPlane(std::move(root_plane));

  setFgColor(styles_.window_fg_color);
  setBgColor(styles_.window_bg_color);
  fill();

  {
    auto buffer_names = controller->getBufferNames();
    vector<string> buffer_display_names;
    buffer_display_names.reserve(buffer_names.size());
    for (auto & name : buffer_names) buffer_display_names.push_back(controller->getBufferDisplayName(name));
    menu_ = make_shared<TerminalMenu>(buffer_names, std::move(buffer_display_names), controller->getSelectedBufferName());
  }

  bool use_pixel = notcurses_check_pixel_support(*nc) != NCPIXEL_NONE;
  auto make_chart = [&](Chart::ChartType type, double min_y, double max_y) -> shared_ptr<Chart> {
    if (use_pixel) return make_shared<TerminalPixelChart>(getPlane(), type, min_y, max_y);
    else return make_shared<TerminalChart>(getPlane(), type, min_y, max_y);
  };
  chart_ = make_chart(Chart::DOTS, 0.0, 0.0);
  volume_meter_ = make_chart(Chart::DOTS, -100, 0);

  if (use_pixel) heatmap_ = make_shared<TerminalPixelHeatmapChart>(getPlane(), DiracAnalyzer::kAzimuthBins, DiracAnalyzer::kElevationBins);
  else heatmap_ = make_shared<TerminalHeatmapChart>(getPlane(), DiracAnalyzer::kAzimuthBins, DiracAnalyzer::kElevationBins);

  // No footer legend on either scope: the heatmap's axis extremes are
  // drawn directly in the grid area instead (axisLabels(), above), and the
  // FFT chart's "Spectrum: 0-{nyquist}Hz" label was removed the same way,
  // reclaiming the row a legend would have used.

  initializeWidgets();

  layout();

  // Every other widget above was created after menu_, so without this the
  // scope charts/pattern editor/status line all sit above it in z-order -
  // see UIMenu::raiseToTop()'s own comment for why that hides an unrolled
  // section's dropdown even though the menu still functions correctly.
  menu_->raiseToTop();

  nc->render();
}

void
TerminalUI::render() {
  nc->render();
}

void
TerminalUI::refresh() {
  nc->refresh(nullptr, nullptr);
}

bool
TerminalUI::readInput() {
  ncinput ni;

  // Applies the same id/modifier massaging this loop has always done, then
  // constructs and dispatches one InputEvent - factored out so a byte the
  // KP-escape-sequence recognizer below ends up replaying (see
  // kp_escape_depth_'s own comment) goes through identical processing to
  // one that was never buffered at all.
  auto dispatchRawKey = [this](int raw_id, int y, int x, unsigned modifiers, InputEvent::Kind kind) {
    bool alt = modifiers & NCKEY_MOD_ALT;
    bool shift = modifiers & NCKEY_MOD_SHIFT;
    bool ctrl = modifiers & NCKEY_MOD_CTRL;
    bool meta = modifiers & NCKEY_MOD_META;

    int id = raw_id;
    int true_case_id = raw_id; // see InputEvent's own comment on this field
    if (id >= 'A' && id <= 'Z') {
      id = tolower(id);
      if (!ctrl) shift = true; // fix bug in notcurses
    } else if (id == 28) {
      ctrl = true;
      alt = meta = shift = false;
      id = '\\';
    }

    InputEvent input(id, y, x, alt, shift, ctrl, meta, kind, true_case_id);
    offerInput(input);
  };

  // Replays every byte the KP-escape recognizer had buffered so far, in
  // order, then resets it - used whenever a partial match turns out not
  // to be the sequence after all.
  auto flushPendingKpEscape = [this, &dispatchRawKey]() {
    for (int i = 0; i < kp_escape_depth_; i++) {
      auto & p = kp_escape_pending_[i];
      dispatchRawKey(p.id, p.y, p.x, p.modifiers, p.kind);
    }
    kp_escape_depth_ = 0;
  };

  // Only the very first dequeue of a batch gets definitely_pending=true -
  // it's the one call directly backed by poll() having just reported
  // notcurses's own input-ready fd readable (see startUI()'s own poll()
  // call, and NotcursesInputEventSource's top comment for why this
  // matters: a genuine Ctrl-Space, codepoint 0, is otherwise
  // indistinguishable from "nothing available" here). Every later
  // dequeue in the same drain burst has no such external guarantee.
  bool first_dequeue = true;

  while (true) {
    // Emacs shows "ESC-" in the echo area while a Meta-prefix wait is
    // open (EscapeSequenceCoalescer::escapePending()'s own comment) -
    // matched here, but not immediately: recorded on the transition, with
    // the actual indicator shown only once escapeIndicatorPollTimeoutMs()'s
    // delay has passed with the wait still open (updateEscapeIndicator(),
    // called from startUI()'s own loop) - a fast Alt-chord that resolves
    // well within that window never flickers the indicator at all.
    bool escape_was_pending = escape_input_->coalescer.escapePending();
    if (!escape_input_->coalescer.next(&ni, first_dequeue)) break;
    first_dequeue = false;
    bool escape_now_pending = escape_input_->coalescer.escapePending();
    if (escape_now_pending && !escape_was_pending) {
      escape_pending_since_ = std::chrono::steady_clock::now();
      escape_indicator_shown_ = false;
    } else if (!escape_now_pending && escape_was_pending) {
      if (escape_indicator_shown_) setStatus("");
      escape_pending_since_.reset();
      escape_indicator_shown_ = false;
    }

    // Legacy terminals only ever report NCTYPE_UNKNOWN (no press/release
    // distinction - notcurses's own signal that this terminal never
    // negotiated the Kitty keyboard protocol at all, no separate
    // capability query needed); the Kitty keyboard protocol (kitty, foot,
    // wezterm, ghostty, ...) reports real NCTYPE_PRESS/NCTYPE_REPEAT/
    // NCTYPE_RELEASE events for the same physical keystroke. Mapped to
    // its own distinct InputEvent::Kind::UNKNOWN (not silently folded
    // into PRESS) - see that enum's own doc comment for why: code that
    // tracks "is this key still held" cannot infer anything of the kind
    // from an UNKNOWN-kind terminal, since it has no way to ever learn
    // that the key was released. RELEASE now reaches offerInput() (it
    // didn't used to - see InputEvent::Kind's own doc comment):
    // UIElement::dispatchCommand() ignores it outright so no keymap-bound
    // command double-fires the way plain presses used to (the bug this
    // code used to guard against by dropping RELEASE entirely), but
    // PatternEditor's own raw note-entry code needs to see it, to send a
    // note-off when a held note key is physically released.
    auto kind = ni.evtype == NCTYPE_RELEASE ? InputEvent::Kind::RELEASE
              : ni.evtype == NCTYPE_REPEAT ? InputEvent::Kind::REPEAT
              : ni.evtype == NCTYPE_PRESS ? InputEvent::Kind::PRESS
              : InputEvent::Kind::UNKNOWN;

    // Legacy xterm/VT220 "SS3 + modifier digit" encoding some terminals
    // still send for a Ctrl-modified numeric-keypad Divide/Multiply
    // keystroke instead of the Kitty keyboard protocol (confirmed via
    // notcurses-input against a real terminal: notcurses's own
    // escape-sequence lexer doesn't recognize this pattern as a single
    // key, so all 4 bytes - ESC, 'O', '5', then 'o' or 'j' - arrive as
    // their own separate, unmodified raw events). Recognized here and
    // turned into one synthetic Ctrl-modified InputEvent
    // (NCKEY_KP_DIVIDE/NCKEY_KP_MULTIPLY) instead of leaking "O5o"/"O5j"
    // through as if it had been typed into the pattern grid. A byte that
    // breaks a partial match falls through to normal dispatch below (via
    // flushPendingKpEscape() replaying whatever was buffered first), so a
    // real standalone Escape or Esc-then-x for M-x still work exactly as
    // before.
    if (kp_escape_depth_ == 0 && ni.id == NCKEY_ESC) {
      kp_escape_pending_[0] = { static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind };
      kp_escape_depth_ = 1;
      continue;
    } else if (kp_escape_depth_ == 1) {
      if (ni.id == 'O') {
	kp_escape_pending_[1] = { static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind };
	kp_escape_depth_ = 2;
	continue;
      }
      flushPendingKpEscape();
    } else if (kp_escape_depth_ == 2) {
      if (ni.id == '5') {
	kp_escape_pending_[2] = { static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind };
	kp_escape_depth_ = 3;
	continue;
      }
      flushPendingKpEscape();
    } else if (kp_escape_depth_ == 3) {
      if (ni.id == 'o' || ni.id == 'j') {
	kp_escape_depth_ = 0;
	InputEvent input(ni.id == 'o' ? NCKEY_KP_DIVIDE : NCKEY_KP_MULTIPLY, ni.y, ni.x, false, false, true, false, kind);
	offerInput(input);
	continue;
      }
      flushPendingKpEscape();
    }

    dispatchRawKey(static_cast<int>(ni.id), ni.y, ni.x, ni.modifiers, kind);
  }

  return true;
}

namespace {

// Song::getCurrentTrackId()'s own id, converted to whichever index-space
// `track_ids` uses - every LaunchpadManager call below still takes a
// plain index (a real per-list position, needed for arithmetic like
// "move one track over" - LaunchpadLayout::advanceTrackIndex() - and for
// auto-growing a brand-new song up to a target count, neither of which a
// bare id supports), just no longer sourced from any one widget's own
// cursor. Falls back to 0 (not -1) when the id isn't found here (unset,
// or a track this particular list doesn't include) - the same "just pick
// the first one" fallback PatternEditor's own cursor already starts at.
int indexOfTrack(const vector<int> & track_ids, int track_id) {
  auto it = find(track_ids.begin(), track_ids.end(), track_id);
  return it == track_ids.end() ? 0 : static_cast<int>(it - track_ids.begin());
}

} // namespace

void
TerminalUI::requestOverviewFocus() {
  // Lands on the overview's own last (rightmost) column, not wherever its
  // cursor happened to be left last time - both entry points (PatternEditor's
  // leftmost track, Launchpad's prev-track already at track 0) arrive
  // "from the right". Not symmetric with exitOverview() (always the first
  // track, regardless of which column was current here) - deliberately:
  // exiting always returns to the same, predictable starting point.
  auto num_tracks = static_cast<int>(arrangement_grid_->getVisibleTrackIds(getController().getSong()).size());
  arrangement_grid_->setCursorTrackIndex(max(0, num_tracks - 1));
  active_element_ = arrangement_grid_;
}

void
TerminalUI::exitOverview() {
  pattern_editor_->setCursorTrack(0);
  active_element_ = pattern_editor_;
}

void
TerminalUI::commitOverviewCell(int track_id, int section_idx, int row) {
  // The whole commit refuses while playing - matches PatternEditor's own
  // move-row-up/move-row-down guard (row navigation only ever runs while
  // stopped), and avoids a commit that silently only did half of what it
  // normally does (move the track but not the playhead) while playing.
  if (getController().getPlaybackInfo().isPlaying()) return;

  auto & song = getController().getSong();
  // NOTE: if PatternEditor's own mark/selection happens to still be active
  // (selection_active_, unrelated to anything this grid/Launchpad does),
  // setEditPosition() below clamps to the pattern that selection is in
  // rather than actually jumping to (section_idx, row) - a real, narrow edge
  // case, not handled here.
  getController().setEditPosition(song.toAbsoluteRow(section_idx, row));

  auto track_ids = song.getRootTrackIds();
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
  arrangement_grid_ = make_shared<ArrangementGrid>(getPlane());
  session_view_ = make_shared<SessionView>(getPlane());
  outline_view_ = make_shared<OutlineView>(getPlane());
  // Enter commits the cell under this grid's own (local, passive) cursor
  // to shared state - see ArrangementGrid.h's own comment on why this is a
  // callback rather than the grid reaching for PatternEditor/
  // active_element_ itself (it doesn't know either exists).
  // launchpad_manager_ isn't set yet at this point (wireLaunchpad() assigns
  // it later, after main.cpp's own ui.initialize()/ui.start() call order -
  // see that method for the equivalent Launchpad wiring), so that half of
  // commitOverviewCell()'s callers is wired there instead.
  arrangement_grid_->setCommitCallback([this](int track_id, int section_idx, int row) { commitOverviewCell(track_id, section_idx, row); });
  // Plain Left with nowhere further left to go - see PatternEditor's own
  // setOverviewRequestCallback() comment; Launchpad's prev-track hits the
  // same edge, wired in wireLaunchpad() below for the same launchpad_manager_-
  // isn't-set-yet reason commitOverviewCell()'s own split wiring is.
  pattern_editor_->setOverviewRequestCallback([this]() { requestOverviewFocus(); });
  // The reverse edge: Right past the last (rightmost) visible column -
  // lands on PatternEditor's own first track, same landing spot Launchpad's
  // own overview-exit already uses (see wireLaunchpad()'s equivalent wiring).
  arrangement_grid_->setExitRightCallback([this]() { exitOverview(); });
  cover_art_ = make_shared<CoverArt>(getPlane());
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

  // Session/overview first, not pattern editing - matches the Launchpad's
  // own Session view as the more approachable starting point for a fresh
  // buffer (a bird's-eye view of tracks/sections) rather than dropping
  // straight into note-by-note editing.
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
  // session-view/outline-view/pattern-viewer are UI's own now (Menu-only -
  // Buffers menu's own "Open Session View"/"Open Outline"/"Open Pattern
  // Viewer" items, no keybinding here) - the buffer-change listener above
  // does the actual screen-slot swap once Controller's own selected buffer
  // changes.
  commands_.define("kill-buffer", [this]() {
    auto doKill = [this]() {
      auto name = getController().getActiveBufferName();
      if (getController().killActiveBuffer()) {
	setStatus("Killed " + name);
      } else {
	setStatus("Can't kill the only open buffer");
      }
    };
    // No prompt needed when the song would stay open under its other
    // aspect (PatternEditor/SessionView) - nothing is actually at risk of
    // being discarded, just this one view closing.
    if (getController().hasUnsavedChanges() && !getController().activeSongHasOtherOpenViews()) {
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

  // C-x C-x (exchange-point-and-mark) is a PatternEditor-owned command (the
  // mark/point state it swaps lives there, alongside set-mark/kill-region -
  // see PatternEditor.cpp's own definition) - forwarded through
  // executeCommand() the same way this class's own active-element fallback
  // would, since the C-x prefix itself is only ever recognized at this
  // level (dispatchCommand() below checks *this* registry, not
  // PatternEditor's - unlike the M-x path, which does go through that
  // fallback chain).
  commands_.define("exchange-point-and-mark", [this]() { pattern_editor_->executeCommand("exchange-point-and-mark"); });
  // merge-clip-to-background/toggle-record-arm are UI's own now (plain
  // Controller::sendCommand() forwarding, same shape save-song's own
  // C-x-reachable wrapper uses) - both target Song::getCurrentTrackId(),
  // not any one widget's own cursor, so they work regardless of which UI
  // widget currently has focus.

  // other-window (C-x o): cycles focus to the next window. Only two
  // focusable panes exist - pattern_editor_ and arrangement_grid_ - so this
  // is a plain toggle between the two rather than a real cycle.
  commands_.define("other-window", [this]() {
    active_element_ = (active_element_.lock() == arrangement_grid_)
      ? std::shared_ptr<UIElement>(pattern_editor_)
      : std::shared_ptr<UIElement>(arrangement_grid_);
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
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('o', false, false, false, false), "other-window");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('m', false, false, false, false), "merge-clip-to-background");
  keymap_.bindPrefixed(ctrl_x, KeyChord::pack('r', false, false, false, false), "toggle-record-arm");
  keymap_.bind(KeyChord::pack(' ', false, false, false, false), "toggle-playing");
  keymap_.bind(KeyChord::pack('[', false, false, false, false), "octave-down");
  keymap_.bind(KeyChord::pack(']', false, false, false, false), "octave-up");

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
    // getSelectedBufferName() (raw, possibly a SessionView/OutlineView
    // aspect name), not getActiveBufferName() (always the real Song) - the
    // menu's own "current" marker must highlight whichever row is
    // literally selected, aspect suffix included.
    auto selected = getController().getSelectedBufferName();
    menu_->refreshBuffers(names, display_names, selected);

    // Cursor/scroll/selection/live-note/annotation-editing state - see
    // PatternEditor::handleBufferChanged()'s own comment. Reads
    // getActiveBufferName() (always canonical) internally, so toggling
    // between a buffer and one of its own aspects looks like no change at
    // all here - correct, since it's the same Song/edit position either
    // way.
    pattern_editor_->handleBufferChanged();

    // SessionView/OutlineView takes over pattern_editor_'s own screen slot
    // exactly while the newly-selected buffer is that aspect - see
    // SessionView.h's/OutlineView.h's own comments. Guarded on an actual
    // change so a buffer switch between two ordinary (non-aspect) buffers,
    // or between two aspects of the same kind, doesn't fight whatever
    // active_element_ already legitimately is (e.g. arrangement_grid_).
    auto new_aspect = getController().isSessionViewBuffer(selected) ? WorkspaceAspect::SESSION_VIEW :
      getController().isOutlineViewBuffer(selected) ? WorkspaceAspect::OUTLINE_VIEW : WorkspaceAspect::PATTERN_EDITOR;
    if (new_aspect != workspace_aspect_) {
      workspace_aspect_ = new_aspect;
      if (new_aspect == WorkspaceAspect::SESSION_VIEW) {
        auto playable = getController().getSong().getPlayableTrackIds();
        session_view_->setCursorTrackIndex(indexOfTrack(playable, getController().getSong().getCurrentTrackId()));
        active_element_ = session_view_;
      } else if (new_aspect == WorkspaceAspect::OUTLINE_VIEW) {
        active_element_ = outline_view_;
      } else {
        active_element_ = pattern_editor_;
      }
      layout();
      // Not a direct renderComponents(true) call here - see
      // force_next_render_'s own comment on TerminalUI.h for why that
      // would silently never actually reach the screen.
      force_next_render_ = true;
    }
  });
}

bool
TerminalUI::executeCommand(std::string_view name) {
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

  constexpr int kHeatmapWidth = 31; // 20 * 1.5, rounded up to the nearest odd width
  constexpr int kScopeRow = 1, kScopeHeight = 5;

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
  cover_art_->resize(kScopeHeight, cover_art_width).move(kScopeRow, 0);

  auto num_tracks = static_cast<int>(getController().getSong().getPlayableTrackIds().size());
  // ArrangementGrid spends 2 columns per track (its own identifier cell
  // plus a shared padding cell - see ArrangementGrid.cpp's own
  // kColWidth) plus 1 more for the leading padding cell before the first
  // track, so its width needs doubling (plus one) here to actually fit
  // the same track count this clamp implies.
  int matrix_width = std::clamp(num_tracks, 4, 24) * 2 + 1;
  int matrix_x = cover_art_divider_x + 1;
  int matrix_divider_x = matrix_x + matrix_width;
  arrangement_grid_->resize(kScopeHeight, matrix_width).move(kScopeRow, matrix_x);

  int chart_x = matrix_divider_x + 1;
  int chart_width = std::max(1, cols - chart_x - 9 - kHeatmapWidth - 2); // -2 for the single-column dividers on either side of the heatmap
  int divider1_x = chart_x + chart_width, divider2_x = divider1_x + 1 + kHeatmapWidth;
  chart_->resize(kScopeHeight, chart_width).move(kScopeRow, chart_x);
  heatmap_->resize(kScopeHeight, kHeatmapWidth).move(kScopeRow, divider1_x + 1);
  volume_meter_->resize(kScopeHeight, 9).move(kScopeRow, divider2_x + 1);

  // Single-column dividers between the five scopes - drawn once here
  // rather than per-frame, since these columns fall outside every scope's
  // own resized rectangle, so nothing else ever repaints over them.
  setFgColor(styles_.window_border_color);
  setBgColor(styles_.window_bg_color);
  for (int row = 0; row < kScopeHeight; row++) {
    putstr(kScopeRow + row, cover_art_divider_x, "│");
    putstr(kScopeRow + row, matrix_divider_x, "│");
    putstr(kScopeRow + row, divider1_x, "│");
    putstr(kScopeRow + row, divider2_x, "│");
  }
  // All three get the exact same real rect always - notcurses itself
  // refuses/ignores a plane resize to zero rows or columns (confirmed via
  // a pty+notcurses reproduction), so shrinking an inactive one to (0, 0)
  // silently no-ops, leaving its last real content and z-position
  // untouched underneath whichever one is actually supposed to show.
  // moveToTop() (below) is what actually decides which one is visible -
  // raising the active widget above its siblings, not the rect itself.
  pattern_editor_->resize(rows - 8, cols).move(6, 0);
  session_view_->resize(rows - 8, cols).move(6, 0);
  outline_view_->resize(rows - 8, cols).move(6, 0);
  switch (workspace_aspect_) {
    case WorkspaceAspect::SESSION_VIEW: session_view_->moveToTop(); break;
    case WorkspaceAspect::OUTLINE_VIEW: outline_view_->moveToTop(); break;
    default: pattern_editor_->moveToTop(); break;
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
}

bool
TerminalUI::renderComponents(bool refresh) {
  // See force_next_render_'s own comment on TerminalUI.h - consumed here
  // (the one call site whose own return value actually reaches startUI()'s
  // nc->render() gate), not at whichever earlier, input-handling call site
  // actually requested it.
  refresh = refresh || force_next_render_;
  force_next_render_ = false;
  bool render = false;
  auto active = active_element_.lock();
  auto & song = getController().getSong();
  // The shared/global track selection - Song's own, not any one widget's
  // cursor (PatternEditor's own root-track-list index doesn't necessarily
  // line up position-for-position with ArrangementGrid's own (color-
  // eligible-only) list, which is why this is passed as a real id below,
  // not a bare index).
  int selected_track_id = song.getCurrentTrackId();

  // "toggle-record-arm"'s own arm-something-new branch reads these two
  // (Controller::isSessionViewFocused()/setSessionViewCursor()) at the
  // moment it actually arms, which can happen from a Launchpad's own CC19
  // press just as easily as from here - dispatched straight to Controller,
  // bypassing this class entirely - so both need to already be correct by
  // then, not computed on demand only when this class itself handles the
  // keypress. Pushed here, once every frame, rather than only on an
  // explicit focus change: simplest way to guarantee "always current"
  // without a second, easy-to-miss update path for every place focus or
  // Session View's own cursor can change.
  bool session_view_focused = workspace_aspect_ == WorkspaceAspect::SESSION_VIEW && active == session_view_;
  getController().setSessionViewFocused(session_view_focused);
  if (session_view_focused) {
    auto track_ids = song.getPlayableTrackIds();
    auto track_index = session_view_->getCursorTrackIndex();
    if (track_index >= 0 && track_index < static_cast<int>(track_ids.size())) {
      getController().setSessionViewCursor(track_ids[static_cast<size_t>(track_index)], session_view_->getCursorClipIndex());
    }
  }

  // Exactly one of pattern_editor_/session_view_/outline_view_ occupies
  // the screen slot all three share (see layout()) - render whichever one
  // workspace_aspect_ says is actually showing, never more than one.
  switch (workspace_aspect_) {
    case WorkspaceAspect::SESSION_VIEW: render |= session_view_->render(styles_, refresh, active == session_view_); break;
    case WorkspaceAspect::OUTLINE_VIEW: render |= outline_view_->render(styles_, refresh, active == outline_view_); break;
    default: render |= pattern_editor_->render(styles_, refresh, active == pattern_editor_); break;
  }
  render |= arrangement_grid_->render(styles_, refresh, active == arrangement_grid_, selected_track_id);
  render |= cover_art_->render(styles_, refresh);
  render |= info_line_->render(styles_, refresh);
  render |= octave_control_->render(styles_, refresh);

  if (launchpad_manager_) {
    auto track_ids = song.getPlayableTrackIds();
    // Populated every call regardless of which UI element actually has
    // focus - a Launchpad's own Session view (CC95/96, per-device) is
    // independent of that now, so this always needs to be ready with
    // wherever a press would actually land (see LaunchpadManager::
    // SessionWindow's own comment).
    LaunchpadManager::SessionWindow session;
    session.track_ids = arrangement_grid_->getVisibleTrackIds(song);
    session.cursor_section_idx = arrangement_grid_->getCursorSection();
    launchpad_manager_->refresh(song, track_ids, getController().getPlaybackInfo(),
      track_ids.empty() ? -1 : indexOfTrack(track_ids, song.getCurrentTrackId()), getController(), session);
  }

  return render;
}

std::shared_ptr<UIElement>
TerminalUI::currentWorkspaceElement() const {
  switch (workspace_aspect_) {
    case WorkspaceAspect::SESSION_VIEW: return session_view_;
    case WorkspaceAspect::OUTLINE_VIEW: return outline_view_;
    default: return pattern_editor_;
  }
}

bool
TerminalUI::tryActivate(int y, int x, std::shared_ptr<UIElement> element) {
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
TerminalUI::offerInput(const InputEvent & input) {
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
    // Deferred, not a direct renderComponents(true) call - see
    // force_next_render_'s own comment on TerminalUI.h: this runs from
    // inside input handling, before startUI()'s own main loop reaches its
    // own renderComponents() call (the one that actually gates
    // nc->render(), the real terminal flush) - a direct call here would
    // draw everything correctly into notcurses's own plane state, then
    // report "nothing left to redraw" right back to that outer call,
    // which would then skip the flush and never actually show it.
    force_next_render_ = true;
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
    // pattern_editor_/session_view_/outline_view_ share one screen rect
    // (see layout()) - only try whichever one is actually showing, never
    // more than one, since a click there must activate whichever is
    // visible, not always pattern_editor_.
    bool activated = tryActivate(input.getY(), input.getX(), currentWorkspaceElement());
    activated = tryActivate(input.getY(), input.getX(), arrangement_grid_) || activated;
    activated = tryActivate(input.getY(), input.getX(), octave_control_) || activated;

    // Fall back to whichever widget currently occupies the main
    // workspace slot if the click landed somewhere no widget claims (e.g.
    // the FFT/heatmap/loudness scope strip, or the dividers between
    // them). Without this, active_element_ was left permanently empty (a
    // real, confirmed bug): every subsequent keyboard command routed
    // through it (offerInput()'s active_element_ fallback below, and
    // Launchpad button commands via executeCommand()) silently no-op'd -
    // including plain Up/Down arrow - until the user happened to click
    // directly back on the workspace.
    if (!activated) active_element_ = currentWorkspaceElement();

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

  // Same union-of-input-sources reasoning as the two calls above - section
  // growth isn't tied to which one is actually recording, so it isn't
  // folded into either's own onRowAdvanced(). isRecording() (mic capture
  // into a SampleTrack clip) is a third, independent input source that
  // needs the same growth - it never touches PatternEditor's/
  // LaunchpadManager's own isAutoRecording() flags at all.
  bool recording = (launchpad_manager_ && launchpad_manager_->isAutoRecording()) ||
                    (pattern_editor_ && pattern_editor_->isAutoRecording()) ||
                    getController().isRecording();
  getController().extendRecordingSectionIfNeeded(recording);

  // Clip::setLength()'s own counterpart to the section growth above - each
  // caller's own note-recording clips (Controller::ensureNoteRecordingClip())
  // grown independently. Deliberately not gated on `recording` at all
  // (unlike extendRecordingSectionIfNeeded() above) - see
  // extendRecordingClipsIfNeeded()'s own comment on why a clip that
  // already exists needs no further proof a genuine session is driving
  // it, and why gating on isAutoRecording() here would silently stop
  // growing a take recorded against playback the performer had already
  // started manually. Each call's own held-track-ids argument scopes
  // growth further, to only a track with a note actually held right now.
  if (pattern_editor_) getController().extendRecordingClipsIfNeeded(pattern_editor_->getAutoRecordClipIds(), pattern_editor_->getActiveNoteTrackIds());
  if (launchpad_manager_) getController().extendRecordingClipsIfNeeded(launchpad_manager_->getAutoRecordClipIds(), launchpad_manager_->getActiveNoteTrackIds());
  // A live mic take's own counterpart to the two calls above - see
  // extendRecordingSampleClipIfNeeded()'s own comment for why it needs no
  // clip_ids/held_track_ids of its own.
  getController().extendRecordingSampleClipIfNeeded();

  ev.redraw();
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
TerminalUI::handleRecordEvent(RecordEvent & ev) {
  if (getController().isRecording()) {
    setStatus(format("recorded {} frames", ev.getData().size()));
    getController().addToSample(ev.getData());
    // Lazily, exactly once per take, but only for a take that was never
    // armed (Controller::isRecordingArmed()) - an armed take's own clip
    // is created by handleRecordingLatencyEvent() below instead, once its
    // own round-trip measurement arrives, so it can be placed/trimmed
    // correctly from the start rather than created here uncompensated
    // and only adjusted afterward.
    if (!getController().hasRecordingClip() && !getController().isRecordingArmed()) {
      getController().beginSampleCapture(getController().getRecordingTrackId());
    }
  }
}

void
TerminalUI::handleRecordingLatencyEvent(RecordingLatencyEvent & ev) {
  // Both guards defensive - Player.cpp only ever pushes this once per
  // take (the false -> true edge of Controller::isRecording()), and
  // hasRecordingClip() being already true would mean a second, spurious
  // measurement somehow arrived for the same take - but this is where
  // the clip actually gets created, trimmed, and placed, all as one step,
  // so it stays defensive rather than assuming either can't happen.
  if (getController().isRecording() && !getController().hasRecordingClip()) {
    getController().beginSampleCapture(getController().getRecordingTrackId(), ev.getLatencyFrames());
  }
}

void
TerminalUI::handleThresholdRecordingTriggeredEvent(ThresholdRecordingTriggeredEvent & ev) {
  // Defensive, same reasoning as handleRecordingLatencyEvent() above -
  // Player.cpp only ever pushes this once per arm cycle (its own
  // threshold_triggered_this_arm_cycle_ latch).
  if (getController().hasRecordingClip()) return;

  // This is where a threshold-triggered take actually begins, as if it
  // had been recording this whole time - startRecording() first (a fresh
  // current_sample), then the ring buffer's own already-captured lead-in
  // prepended into it, then the snapshotted (backdated - see the event's
  // own comment) start position armed for beginSampleCapture() to place
  // at.
  getController().startRecording();
  getController().addToSample(ev.getPreroll());
  // Never for a Session View take (isSessionRecording()) - that populates
  // a clip slot directly with no arrangement position at all, so there's
  // nothing here to snapshot; beginSampleCapture() already treats
  // recording_start_section_'s own untouched -1 default as "stays unplaced."
  if (!getController().isSessionRecording()) getController().armRecordingStart(ev.getSection(), ev.getRow());
  getController().beginSampleCapture(ev.getTrackId());
  getController().clearThresholdArmed();
}

void
TerminalUI::handleLogEvent(LogEvent & ev) {
  setStatus(ev.getText());
}

void
TerminalUI::handleMidiEvent(MidiEvent & ev) {
  pattern_editor_->handleMidiEvent(ev);
}

void
TerminalUI::handleLaunchpadPadEvent(LaunchpadPadEvent & ev) {
  // Track-picker overlay (opened by CC49 "Stop Clip"/CC39 "Mute"/CC29
  // "Solo" - see LaunchpadManager::handleRawButton()'s own comment) -
  // Session-view-only, so this only ever intercepts the picker row itself
  // while it's open; every other row (Session view's own content) falls
  // through to the normal SESSION handling below unchanged, staying fully
  // interactive underneath the overlay.
  if (launchpad_manager_ && launchpad_manager_->isTrackPickerRow(ev.getDeviceIndex(), ev.getY())) {
    launchpad_manager_->handleTrackPickerPadEvent(ev, getController());
    return;
  }
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
  // handlePadEvent() itself indexes song.getPlayableTrackIds() with this
  // - see indexOfTrack()'s own comment for why a real index, not the bare
  // id, is still what it needs.
  auto track_ids = getController().getSong().getPlayableTrackIds();
  launchpad_manager_->handlePadEvent(ev, getController(),
    indexOfTrack(track_ids, getController().getSong().getCurrentTrackId()), pattern_editor_->getEditStepSize());
}

void
TerminalUI::handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) {
  if (!launchpad_manager_) return;

  auto device_id = ev.getDeviceIndex();

  // CC98 ("Capture MIDI", DRAW mode's own home now - see
  // LaunchpadManager::GridMode's own comment) needs press and release, not
  // just press - its own tap-vs-long-hold toggle/blank-canvas gesture
  // (LaunchpadManager::handleDrawToggleButton()). Routed here before the
  // press-only filter below, which every other raw-CC button (and every
  // other release) still goes through unchanged. CC49 ("Stop Clip") and
  // CC97 ("Custom") don't need this - both are plain press-only toggles,
  // handled by handleRawButton() alongside Session/Note below.
  if (ev.getCCNumber() == 98) {
    launchpad_manager_->handleDrawToggleButton(device_id, ev.getKind() == LaunchpadButtonEvent::PRESS);
    return;
  }

  if (ev.getKind() != LaunchpadButtonEvent::PRESS) return;

  // Send A/B: a direct hardware-state toggle (this device's own transient
  // grid-display mode), never a command - intercepted here, by raw CC
  // number, before any command-name resolution happens at all. See
  // LaunchpadManager::handleRawButton's own comment. track_id resolved
  // the same way CC19's own SampleTrack case needs it.
  {
    auto track_ids = getController().getSong().getPlayableTrackIds();
    auto track_id = launchpad_manager_->resolveTrackId(device_id, track_ids, indexOfTrack(track_ids, getController().getSong().getCurrentTrackId()));
    if (launchpad_manager_->handleRawButton(ev.getCCNumber(), device_id, getController(), track_id)) return;
  }

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
  getController().setPendingCommandTrack(launchpad_manager_->resolveTrackId(device_id, track_ids, indexOfTrack(track_ids, getController().getSong().getCurrentTrackId())));

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
  bool handled = launchpad_manager_->handleCommand(*name, device_id, indexOfTrack(track_ids, getController().getSong().getCurrentTrackId()), static_cast<int>(track_ids.size()), getController());
  if (!handled) handled = executeCommand(*name);

  getController().setPendingCommandTrack(-1);
}

void
TerminalUI::wireLaunchpad(LaunchpadManager & launchpad_manager) {
  // launchpad_manager_ itself is already set by UI::start() before this
  // hook runs.
  // "move-row-up"/"move-row-down" while in GridMode::SESSION move
  // ArrangementGrid's own section cursor instead of scrolling a pad-grid row
  // window - see LaunchpadManager::session_move_section_callback_'s own
  // comment for why.
  launchpad_manager.setSessionMoveSectionCallback([this](int delta) { arrangement_grid_->moveCursorSection(getController().getSong(), delta); });
  // "next-track"/"prev-track" outside GridMode::SESSION move the one
  // shared cursor every connected Launchpad follows - see
  // LaunchpadManager::track_move_callback_'s own comment for why. Also
  // moves SessionView's own cursor, kept in step the same way it already
  // seeds from PatternEditor's cursor when Session view first opens
  // (setCursorTrackIndex()'s own comment) - a track change made on the
  // Launchpad has to be reflected there too, not just in PatternEditor,
  // regardless of which one currently has terminal focus. session_ids
  // (the same track_ids getPlayableTrackIds() column order is keyed by)
  // isn't available here, but PatternEditor's own cursor is already an
  // index into that identical ordering, so no translation is needed.
  launchpad_manager.setTrackMoveCallback([this](int new_track_index) {
    pattern_editor_->setCursorTrack(new_track_index);
    session_view_->setCursorTrackIndex(new_track_index);
  });
  // SessionView's own Enter key - acts exactly like a Launchpad Session
  // view pad press on the same cell (LaunchpadManager::
  // triggerSessionClip(), same as handleSessionPadEvent() itself resolves
  // to) - launchpad_manager_ isn't set until this method runs (see
  // arrangement_grid_'s own commit callback comment in initializeWidgets()
  // for why that half is wired there instead).
  session_view_->setTriggerCallback([this](int track_id, int clip_index) {
    launchpad_manager_->triggerSessionClip(getController(), track_id, clip_index);
  });
  // Record Arm's own drum-machine-track repurposing ("toggle-record-arm",
  // Controller.cpp) - opening a clip (Controller::setFocusedClip()) moves
  // the shared track cursor to it (so PatternEditor's/the Launchpad's own
  // fallback_track_index-following resolve there next), forces every
  // connected device's own display to the step grid regardless of
  // whatever GridMode it happened to be in, and gives each one its own
  // default page into the clip (LaunchpadManager::resetDrumEditPaging() -
  // several Launchpads split a clip longer than 8 steps between them
  // without anyone paging by hand first); closing one (a second press on
  // the clip already open - Controller::clearFocusedClip()) hands every
  // connected device back to Session view instead, rather than leaving it
  // stuck showing a step grid with nothing left focused to edit there.
  getController().setDrumEditRequestListener([this](int track_id, bool opened) {
    if (opened) {
      auto & song = getController().getSong();
      auto track_ids = song.getRootTrackIds();
      auto it = std::find(track_ids.begin(), track_ids.end(), track_id);
      if (it != track_ids.end()) pattern_editor_->setCursorTrack(static_cast<int>(it - track_ids.begin()));
      launchpad_manager_->forceNotesModeOnAllDevices();
      launchpad_manager_->resetDrumEditPaging();
      // Meant to be heard in isolation - stops the transport if it
      // happens to be running (the focused-clip audition below only ever
      // engages while stopped anyway - LaunchpadManager::refresh()'s own
      // audition_active) and releases whatever any other track's own
      // Session-View-triggered clip was still sounding, rather than
      // layering the drum edit preview under either one.
      if (getController().getPlaybackInfo().isPlaying()) getController().togglePlaying();
      launchpad_manager_->silenceOtherTriggeredClips(getController());
    } else {
      launchpad_manager_->forceSessionModeOnAllDevices();
    }
  });
}

void
TerminalUI::startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) {
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

  size_t num_midi_capture_desc = audio.getMidiCaptureDescriptors().size();
  auto launchpad_descriptors = launchpad_io.getPollDescriptors();
  size_t num_launchpad_desc = launchpad_descriptors.size();
  size_t midi_base = 3;
  size_t launchpad_base = midi_base + num_midi_capture_desc;
  size_t num_descriptors = launchpad_base + num_launchpad_desc;
  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);

#if 1
  descriptors[0].fd = nc->get_inputready_fd();
#else
  descriptors[0].fd = 0;
#endif
  descriptors[0].events = POLLIN;

  descriptors[1].fd = getController().getUIEventQueue().getPollFd();
  descriptors[1].events = POLLIN;

  descriptors[2].fd = out_pipe[0];
  descriptors[2].events = POLLIN;

  for (size_t i = 0; i < num_midi_capture_desc; i++) {
    descriptors[midi_base + i] = audio.getMidiCaptureDescriptors()[i];
  }

  for (size_t i = 0; i < num_launchpad_desc; i++) {
    descriptors[launchpad_base + i] = launchpad_descriptors[i];
  }

  // setStatus("Starting... nd = " + to_string(num_descriptors));

  renderComponents(true);

  string waiting_stderr;
  
  while ( !close_ui_ ) {
    bool render = false;

    updateEscapeIndicator();

    // setStatus("polling");
    if (poll(descriptors.get(), num_descriptors, escapeIndicatorPollTimeoutMs()) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (i == 0) {
	    render |= readInput();
	  } else if (i == 1) {
	    auto event = getController().getUIEventQueue().pop();
	    handleEvent(*event);
	    if (event->needRedraw()) render = true;
	    while ( getController().getUIEventQueue().hasEvents() ) {
	      auto next_event = getController().getUIEventQueue().pop();
	      handleEvent(*next_event);
	      if (next_event->needRedraw()) render = true;
	    }
	  } else if (i == 2) {
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
	    while ( 1 ) {
	      auto pos = waiting_stderr.find('\n');
	      if (pos != string::npos) {
		setStatus(waiting_stderr.substr(0, pos));
		waiting_stderr.erase(0, pos + 1);
	      } else {
		break;
	      }
	    }
	  } else if (i < launchpad_base) {
	    auto evs = audio.recordMIDI();
	    // setStatus("got midi events: " + to_string(evs.size()));

	    for (auto & ev : evs) {
	      handleEvent(ev);
	      if (ev.needRedraw()) render = true;
	    }
	  } else {
	    auto evs = launchpad_io.pollEvents();

	    for (auto & ev : evs) {
	      handleEvent(*ev);
	      if (ev->needRedraw()) render = true;
	    }
	  }
	}
      }
      
      render |= renderComponents();

      if (render) {
	// Reasserted every frame, not just once at startup: the scope
	// charts'/heatmap's own plot_plane_ (TerminalChart::setSample(),
	// above) is created lazily on first real sample data, and destroyed/
	// recreated again on every resize - each such plane is a fresh
	// sibling of menu_'s (both bind directly to the real stdplane, not
	// to their logical parent widget - see TerminalPlane::createChild()),
	// so it lands on top of menu_ again the moment it's (re)created,
	// silently re-hiding an unrolled section's dropdown. See
	// UIMenu::raiseToTop()'s own comment for the base z-order issue this
	// guards against.
	menu_->raiseToTop();
	nc->render();
      }
    }
  }
}
