#ifndef _OUTLINEVIEW_H_
#define _OUTLINEVIEW_H_

#include "UIElement.h"
#include "../model/TrackType.h"

#include <vector>
#include <string>

class StyleProvider;

struct outline_row_s {
  int level = 0;
  TrackType type = TrackType::UNKNOWN;
  std::string label;
  // Non-empty only for a Library > Instruments row - the literal/taxonomy
  // name InstrumentProvider::tryGetByLiteralName()/resolvePath() can
  // resolve back to a real Instrument. Doubles as this row's own "is this
  // a library instrument" test (offerInput()'s keyboard-audition and
  // NCKEY_ENTER handling both key off it being non-empty).
  std::string library_instrument_path;
};

// A read-only, indented tree of the active song - Song/Instruments/
// per-instrument rows/Tracks/per-track rows, then Library/Rhythms/
// Instruments - the same "collapsible headings" shape Emacs's own
// outline-mode shows for a text buffer, applied to the song structure
// instead. Its own buffer aspect (Controller.h's `BufferAspect::
// OUTLINE_VIEW`, opened via "outline-view" - the Buffers menu's "Open
// Outline" item), takes over pattern_editor_'s own screen region while
// open exactly like SessionView does (see UI::layout()/renderComponents()
// and SessionView.h's own comment) - closed implicitly by any buffer
// switch, not a dedicated close command.
//
// A Library > Instruments row is more than just a label: any note-
// producing keystroke previews that instrument directly (PlaybackControlEvent::
// PREVIEW_NOTE/PREVIEW_STOP - not routed through any Track/buffer, since
// the instrument isn't part of the song yet), and NCKEY_ENTER commits it
// for real, adding it to the active song's own instrument pool
// (Song::addInstrument()) - see offerInput()'s own comments for both.
class OutlineView : public UIElement {
 public:
  OutlineView(UIPlane & parent) : UIElement(parent) {

  }

  bool offerInput(const InputEvent & input) override;
  bool render(const StyleProvider & styles, bool refresh, bool focused);

protected:
  void renderRow(const StyleProvider & styles, int row, bool highlight);

 private:
  // Common tail of NCKEY_UP/DOWN/PGUP/PGDOWN - moves the cursor by `delta`
  // rows (clamped to data_'s own extent) and keeps it scrolled into view.
  void moveCursorBy(int delta);
  // NCKEY_ENTER on a Library > Instruments row - see this class's own
  // header comment. A no-op on any other row (nothing else has a default
  // action yet).
  void addSelectedLibraryInstrumentToPool();

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
