#ifndef _OUTLINEVIEW_H_
#define _OUTLINEVIEW_H_

#include "UIElement.h"
#include "../model/TrackType.h"

#include <vector>
#include <string>
#include <utility>

class StyleProvider;

// What a row's own default action(s) - NCKEY_ENTER/NCKEY_DEL, and what
// renderDetailsPanel() shows for it - target. SECTION covers every plain
// heading row (Song, Library, Tracks, a group name, ...), which has none.
enum class OutlineRowKind { SECTION, TRACK, POOL_INSTRUMENT, LIBRARY_INSTRUMENT, LIBRARY_GROOVE };

struct outline_row_s {
  int level = 0;
  TrackType type = TrackType::UNKNOWN;
  OutlineRowKind kind = OutlineRowKind::SECTION;
  std::string label;
  // TRACK: the track's own internal id (Song::removeTrack()'s own
  // argument). POOL_INSTRUMENT: its index into the instrument pool
  // (Song::removeInstrument()'s own argument). -1 for every other kind.
  int ref_id = -1;
  // LIBRARY_INSTRUMENT: the literal/taxonomy name InstrumentProvider::
  // tryGetByLiteralName()/resolvePath() can resolve back to a real
  // Instrument. LIBRARY_GROOVE: a GroovePatternLibrary.h entry's own name
  // (findGroovePattern()). Empty for every other kind.
  std::string ref_name;
};

// What clicking a Details panel line does, if anything - see
// OutlineView::buildDetailsLines()/handleClick(). NONE covers a line
// with nothing to click (an informational hint like "[note keys]
// Preview", a blank spacer, a wrapped description line).
enum class DetailsAction { NONE, DELETE, ADD_TO_SONG, PREVIEW, STOP };

struct DetailsLine {
  std::string text;
  DetailsAction action = DetailsAction::NONE;
};

// A read-only, indented tree of the active song - Song/Instruments/
// per-instrument rows/Tracks/per-track rows, then Library/Grooves/
// Instruments - the same "collapsible headings" shape Emacs's own
// outline-mode shows for a text buffer, applied to the song structure
// instead. Its own buffer aspect (Controller.h's `BufferAspect::
// OUTLINE_VIEW`, opened via "outline-view" - the Buffers menu's "Open
// Outline" item), takes over pattern_editor_'s own screen region while
// open exactly like SessionView does (see UI::layout()/renderComponents()
// and SessionView.h's own comment) - closed implicitly by any buffer
// switch, not a dedicated close command.
//
// A shared one-row heading strip along the top, split by a vertical
// divider into "Outline" (left) and "Details" (right); below that, the
// scrollable tree on the left and a fixed-width details panel on the
// right, sharing that same divider column - see kDetailsPanelWidth. The
// details panel shows whatever the cursor's own row supports doing -
// Delete for a Track/pool Instruments row (Song::removeTrack()/
// removeInstrument()), Add to Song plus (for a groove) its own
// description for a Library row. Blank for a plain section heading,
// which has no action of its own.
//
// A Library row is more than just a label: any note-producing keystroke
// previews a Library > Instruments row's own instrument directly
// (PlaybackControlEvent::PREVIEW_NOTE/PREVIEW_STOP), 'p' loops a Library >
// Grooves row's own pattern (PREVIEW_GROOVE/PREVIEW_STOP), 'a' stops
// either - none of this routed through any Track/buffer, since nothing
// being previewed is part of the song yet - and NCKEY_ENTER commits
// either kind for real: an instrument joins the active song's own
// instrument pool (Song::addInstrument()), a groove becomes a real Clip
// on a (created-if-needed) PercussionTrack (Song::addClip()) - see
// offerInput()'s own comments for all of this. NCKEY_DEL/NCKEY_BACKSPACE
// on a Track or pool Instruments row removes it outright - no Clone yet
// (that wants real undo scaffolding first, not a one-off here).
class OutlineView : public UIElement {
 public:
  OutlineView(UIPlane & parent) : UIElement(parent) {

  }

  bool offerInput(const InputEvent & input) override;
  bool render(const StyleProvider & styles, bool refresh, bool focused);

protected:
  void renderRow(const StyleProvider & styles, int row, bool highlight);
  // The shared one-row title strip at the very top - static text (plus
  // the divider column, drawn here too since neither side of it changes
  // without a full refresh), only ever needs (re)drawing on one.
  void renderHeading(const StyleProvider & styles);
  // The fixed-width column to the right of the divider - see this
  // class's own header comment. Redrawn in full whenever the cursor moves
  // to a new row (not incrementally, unlike renderRow() above) - its
  // content depends on that row's own kind, not on any per-row diff worth
  // tracking.
  void renderDetailsPanel(const StyleProvider & styles, int details_x, int details_width);

 private:
  // Common tail of NCKEY_UP/DOWN/PGUP/PGDOWN - moves the cursor by
  // `delta` rows (clamped to data_'s own extent) and keeps it scrolled
  // into view.
  void moveCursorBy(int delta);
  // The scroll-wheel's own tail - moves new_scroll_pos_ (the viewport)
  // by `delta` rows without ever touching new_cursor_row_ (the
  // selection), unlike moveCursorBy() above - scrolling to look at more
  // of the tree shouldn't drag the current selection along with it.
  // Clamped so the view can never scroll past the point where the last
  // row is already fully visible.
  void scrollBy(int delta);
  // The Details panel's own content for `row`, line by line - the single
  // source both renderDetailsPanel() (drawing) and handleClick() (hit-
  // testing a click against whichever line it lands on) build from, so
  // the two can never show/dispatch different things for the same row.
  // `details_width` is only needed to word-wrap a groove's own
  // description to the panel's current width.
  std::vector<DetailsLine> buildDetailsLines(const outline_row_s & row, int details_width) const;
  // NCKEY_BUTTON1 - hit-tests the click against whichever of the tree/
  // Details panel it landed in (see this class's own header comment) and
  // either moves the cursor straight to the clicked row (tree side) or
  // runs whatever DetailsAction that Details line carries, if any (panel
  // side). Resolved on RELEASE only, matching SpinBox's own click
  // convention - PRESS is consumed (returns true) but otherwise a no-op.
  bool handleClick(const InputEvent & input);
  // The actual effect behind a DetailsAction - shared by handleClick()
  // above and every keyboard path that already triggers the same thing
  // (NCKEY_ENTER/NCKEY_DEL/'p'/'a' in offerInput()), so a click and its
  // keyboard equivalent can never drift apart.
  void runDetailsAction(DetailsAction action);
  // NCKEY_ENTER on a Library > Instruments row - see this class's own
  // header comment. A no-op on any other row (delegates to
  // addSelectedLibraryGrooveToSong() for a Grooves row instead).
  void addSelectedLibraryInstrumentToPool();
  // NCKEY_ENTER on a Library > Grooves row - see this class's own header
  // comment. Finds the song's own first PercussionTrack (root tracks
  // only, matching this view's own shallow Tracks listing), creating one
  // if none exists yet, and adds a new Clip there seeded from the
  // template's own hits. A no-op on any other row.
  void addSelectedLibraryGrooveToSong();
  // NCKEY_DEL/NCKEY_BACKSPACE on a Track or pool Instruments row - see
  // this class's own header comment. A no-op on any other row, and on a
  // song's own last remaining root track (same floor PatternEditor's
  // "delete-track" command holds - docs/known_bugs.md's own
  // zero-root-tracks entry).
  void deleteSelectedRow();

  // The details panel's own fixed content width (not counting the
  // divider column itself) - see this class's own header comment. Capped
  // to a third of the widget's own width in layout() below, so a narrow
  // terminal never loses the tree entirely to it.
  static constexpr int kDetailsPanelWidth = 36;
  // This widget's own current column split: `first` is the tree's own
  // width (columns [0, first)), the divider sits at column `first`
  // itself, and the details panel's own content spans
  // [first + 1, second). Recomputed from getDim() on every call (cheap,
  // and this widget's own size can change - NCKEY_RESIZE - between
  // render() calls) rather than cached, so render()/renderRow()/
  // renderDetailsPanel()/moveCursorBy() can never disagree about where
  // the divider actually is.
  std::pair<int, int> columnSplit() const;
  // How many of this widget's own rows the scrollable tree (and the
  // details panel alongside it) gets below the shared heading row. Never
  // negative, even on a pathologically short screen rect.
  int treeRows() const;

  std::vector<struct outline_row_s> data_;
  int current_song_version_ = 0;
  bool current_focused_ = false;
  int new_scroll_pos_ = 0, current_scroll_pos_ = 0;
  int new_cursor_row_ = 0, current_cursor_row_ = 0;
  // The physical key id currently sounding a Library-instrument preview
  // note (see PlaybackControlEvent::PREVIEW_NOTE/PREVIEW_STOP's own
  // comment), or -1 when nothing is held - mirrors PatternEditor's own
  // active_keyboard_notes_, just monophonic (a browser previewing one
  // instrument at a time has no need for that map's real per-key
  // polyphony) and keyed by nothing but the key id, since there's no
  // note-column/track identity to remember alongside it here.
  int held_preview_key_ = -1;
};

#endif
