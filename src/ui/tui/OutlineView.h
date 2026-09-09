#ifndef _OUTLINEVIEW_H_
#define _OUTLINEVIEW_H_

#include "../UIElement.h"
#include "../../model/TrackType.h"

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
// TOGGLE_TARGET_PICKER opens/closes a Library > Grooves row's own
// target-track picker - a real floating ncselector plane (see
// OutlineView::openTargetPicker()), not another Details panel line, so
// picking one of its candidates never reaches here at all.
enum class DetailsAction { NONE, DELETE, ADD_TO_SONG, PREVIEW, STOP, TOGGLE_TARGET_PICKER };

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
// Delete plus note-key preview for a Track/pool Instruments row
// (Song::removeTrack()/removeInstrument()), Add to Song plus preview and
// (when one's available - see buildDetailsLines()) its own description
// for a Library row. Blank for a plain section heading, which has no
// action of its own.
//
// Both a Song > Instruments (pool) row and a Library > Instruments row
// are more than just a label: any note-producing keystroke previews the
// row's own instrument directly (PlaybackControlEvent::PREVIEW_NOTE for a
// Library row, PREVIEW_POOL_NOTE for a pool row - the latter resolves the
// exact pool slot, generator overrides/custom Oscillator parameters
// included, rather than re-resolving a name), 'p' loops a Library >
// Grooves row's own pattern (PREVIEW_GROOVE/PREVIEW_STOP), 'a' stops
// whichever of these is currently sounding - none of this routed through
// any Track/buffer for a Library row, since nothing being previewed is
// part of the song yet - and NCKEY_ENTER commits a Library row for real:
// an instrument joins the active song's own instrument pool
// (Song::addInstrument()), a groove becomes a real Clip on whichever
// PercussionTrack its own target picker currently names (see
// openTargetPicker()'s own comment - 't' or a click opens it as a real
// floating plane over this one, a candidate picked by clicking it or
// Enter) - Song::addClip(). NCKEY_DEL/NCKEY_BACKSPACE on a Track or pool
// Instruments row removes it outright - no Clone yet (that wants real
// undo scaffolding first, not a one-off here).
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
  // (NCKEY_ENTER/NCKEY_DEL/'p'/'a'/'t' in offerInput()), so a click and its
  // keyboard equivalent can never drift apart.
  void runDetailsAction(DetailsAction action);
  // NCKEY_ENTER on a Library > Instruments row - see this class's own
  // header comment. A no-op on any other row (delegates to
  // addSelectedLibraryGrooveToSong() for a Grooves row instead).
  void addSelectedLibraryInstrumentToPool();
  // NCKEY_ENTER on a Library > Grooves row - see this class's own header
  // comment. Targets resolveTargetTrackId()'s own current choice - an
  // existing PercussionTrack (root tracks only, matching this view's own
  // shallow Tracks listing) picked via the row's own inline target
  // picker, or a freshly created one if that resolves to
  // kNewTrackTargetId (the default with no existing PercussionTrack, or
  // an explicit "New track" pick) - and adds a new Clip there seeded from
  // the template's own hits. A no-op on any other row.
  void addSelectedLibraryGrooveToSong();
  // Every root PercussionTrack currently in data_ (TRACK rows only, not
  // the underlying Song - data_'s own labels are already the exact
  // "T<N> name" text the tree itself shows, so the picker's candidate
  // list reads identically) - resolveTargetTrackId()/openTargetPicker()'s
  // own shared source for "what can a groove clip target".
  std::vector<const outline_row_s *> compatibleTargetTrackRows() const;
  // The target-track picker's own current choice, re-resolved every call
  // rather than trusted at face value: selected_target_track_id_ might
  // name a track that's since been deleted (falls back the same way a
  // never-yet-chosen selection does) or kNewTrackTargetId explicitly
  // (passed straight through - always a valid choice). Falls back to the
  // first compatibleTargetTrackRows() entry, else kNewTrackTargetId if
  // there are none.
  int resolveTargetTrackId() const;
  // "New track" for kNewTrackTargetId, else whatever data_'s own TRACK row
  // for that id shows as its label - see compatibleTargetTrackRows()'s own
  // comment on why that's the exact text to reuse here.
  std::string targetTrackLabel(int track_id) const;
  // TOGGLE_TARGET_PICKER on an already-closed picker - creates the real
  // floating plane (UIPlane::showPicker()), anchored directly under the
  // "[t] Target: ..." Details panel line (always that row's own fixed
  // screen position for a Library > Grooves row - see buildDetailsLines()),
  // and populates it from compatibleTargetTrackRows() plus "New track".
  // Sized down (never past this widget's own bottom edge) rather than
  // trusting there's always room for every candidate at once.
  void openTargetPicker();
  // TOGGLE_TARGET_PICKER on an already-open picker, or Ctrl-g while it's
  // open - destroys the floating plane without changing
  // selected_target_track_id_.
  void closeTargetPicker();
  // Enter, or a click landing on an item, while the picker is open (see
  // offerInput()'s own pickerActive() handling) - `selection` is
  // UIPlane::getPickerSelection()'s own return, read *before*
  // closeTargetPicker() destroys the plane it comes from. Resolves back
  // to a real track id by matching against compatibleTargetTrackRows()'s
  // own labels (exactly what was handed to addItem() as each row's own
  // id) or the literal "New track" text; a miss (nothing was ever
  // highlighted - no items, or the plane was closed with none selected)
  // leaves selected_target_track_id_ untouched.
  void applyTargetPickerSelection(const std::string & selection);
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
  // The target-track picker's own "create a fresh PercussionTrack instead
  // of using an existing one" choice - 0 is safe to use as a sentinel
  // since real track ids start at 1 (SongObject.cpp's own next_id).
  static constexpr int kNewTrackTargetId = 0;
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
  // The Library > Grooves target-track picker's own choice (see
  // openTargetPicker()) - a single global preference, not per-groove:
  // "which track should Add to Song use" is one ongoing choice, not
  // something worth remembering separately per template. -1 means "never
  // explicitly chosen yet" (resolveTargetTrackId() then defaults to the
  // first compatibleTargetTrackRows() entry, or kNewTrackTargetId with
  // none); kNewTrackTargetId is itself a real, sticky choice once picked,
  // same as any other track id. Whether the picker plane itself is
  // currently open is never cached here - getPlane().pickerActive() is
  // the one source of truth, so it can never drift out of sync with the
  // plane's own real state.
  int selected_target_track_id_ = -1;
  // Set whenever the Details panel's own content needs redrawing despite
  // neither render_all's own triggers nor the cursor moving - the target-
  // track picker committing a new choice, the one case of that today
  // (applyTargetPickerSelection()). Checked/cleared in render() the same
  // as cursor_changed.
  bool details_dirty_ = false;
};

#endif
