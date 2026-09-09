#include "OutlineView.h"

#include "../Controller.h"
#include "../model/Song.h"
#include "../model/SongStructure.h"
#include "../model/PercussionTrack.h"
#include "../model/GroovePatternLibrary.h"
#include "../instruments/GenericInstrument.h"
#include "../instruments/GmInstrumentDescriptions.h"
#include "../playback/InputEvent.h"
#include "../playback/PlaybackControlEvent.h"
#include "../util/constants.h"
#include "../util/Utf8.h"
#include "StyleProvider.h"

#include <algorithm>
#include <unordered_map>
#include <fmt/core.h>

using namespace std;

namespace {

// A plain greedy word-wrap, breaking only at spaces (never mid-word) -
// good enough for the details panel's own short, plain-English groove
// descriptions (GroovePatternTemplate::description), not a general
// typesetting routine. Uses Utf8::displayWidth() per candidate line, not
// a raw byte-length count, for the same reason every other width
// comparison in this codebase does (a multi-byte UTF-8 character - e.g.
// "güira" in one of the groove descriptions - is one display column, not
// several bytes' worth of them).
vector<string> wrapText(const string & text, int width) {
  vector<string> lines;
  if (width <= 0) return lines;

  string current;
  size_t pos = 0;
  while (pos <= text.size()) {
    auto space = text.find(' ', pos);
    auto word = text.substr(pos, space == string::npos ? string::npos : space - pos);
    auto candidate = current.empty() ? word : current + " " + word;
    if (Utf8::displayWidth(candidate) <= width) {
      current = move(candidate);
    } else {
      if (!current.empty()) lines.push_back(current);
      current = move(word);
    }
    if (space == string::npos) break;
    pos = space + 1;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

} // namespace

bool
OutlineView::render(const StyleProvider & styles, bool refresh, bool focused) {
  bool render_all = refresh;
  auto & song = getController().getSong();
  auto & instrument_provider = getController().getInstrumentProvider();

  data_.clear();
  data_.push_back( { 0, TrackType::MASTER, OutlineRowKind::SECTION, "Song" });
  data_.push_back( { 1, TrackType::UNKNOWN, OutlineRowKind::SECTION, "Instruments" });
  for (size_t i = 0; i < song.getInstrumentPool().getInstruments().size(); i++) {
    auto & instrument = *(song.getInstrumentPool().getInstruments()[i]);
    outline_row_s row;
    row.level = 2;
    row.type = TrackType::INSTRUMENT;
    row.kind = OutlineRowKind::POOL_INSTRUMENT;
    row.label = instrument.getDisplayName();
    row.ref_id = static_cast<int>(i);
    data_.push_back(std::move(row));
  }
  data_.push_back( { 1, TrackType::UNKNOWN, OutlineRowKind::SECTION, "Tracks" });
  // "T<N>" (N = color_ordinal_, the same 0-based, color-eligible-leaf-only
  // count PatternEditor/SessionView/ArrangementGrid already show next to
  // this exact track elsewhere - see PatternEditor.cpp's own header-row
  // comment) followed by the artist's own name, mirroring that same
  // convention exactly; a track with no color ordinal at all (a top-level
  // Group/Effect wrapper, not itself color-eligible) just falls back to
  // its own display name, with no "T<N>" to show.
  SongStructure structure(song);
  for (size_t i = 0; i < song.getMasterTrack().getChildren().size(); i++) {
    auto & track = song.getMasterTrack().getChildren()[i];
    auto & baseline = structure.getBaselineInfo(track->getInternalId());
    string label;
    if (baseline.color_ordinal_ >= 0) {
      label = "T" + to_string(baseline.color_ordinal_);
      if (!track->getName().empty()) label += " " + track->getName();
    } else {
      label = track->getDisplayName();
    }
    outline_row_s row;
    row.level = 2;
    row.type = track->getType();
    row.kind = OutlineRowKind::TRACK;
    row.label = label;
    row.ref_id = track->getInternalId();
    data_.push_back(std::move(row));
  }
  data_.push_back( { 0, TrackType::UNKNOWN, OutlineRowKind::SECTION, "Library" });
  // One level-1 heading per GroovePatternTemplate::group actually present
  // (today just "Grooves") - a direct child of Library, not nested under
  // any further "Clips" grouping (there's nothing else under that name to
  // group it with) - derived from the library itself rather than
  // hardcoded, so a future group (e.g. a bass-line companion) shows up
  // here without touching this loop. Relies on same-group entries being
  // stored contiguously in getGroovePatternLibrary()'s own table (they
  // are, by construction) rather than sorting/grouping them itself.
  {
    string current_group;
    for (auto & pattern : getGroovePatternLibrary()) {
      if (pattern.group != current_group) {
        current_group = pattern.group;
        data_.push_back( { 1, TrackType::UNKNOWN, OutlineRowKind::SECTION, current_group } );
      }
      outline_row_s row;
      row.level = 2;
      row.kind = OutlineRowKind::LIBRARY_GROOVE;
      row.label = pattern.name;
      row.ref_name = pattern.name;
      data_.push_back(std::move(row));
    }
  }
  // Empty for now - synthesized ambient textures (waves, campfire,
  // thunder, rain, ...) belong here once that content exists; no entries
  // yet.
  data_.push_back( { 1, TrackType::UNKNOWN, OutlineRowKind::SECTION, "Ambience" } );
  data_.push_back( { 1, TrackType::UNKNOWN, OutlineRowKind::SECTION, "Instruments" });
  // The curated taxonomy paths (e.g. "piano.acoustic.grand"), not
  // getInstruments()'s own "native:"-namespaced SF2 preset names - see
  // InstrumentProvider::getTaxonomyPaths()'s own doc comment. Sorted:
  // unordered_map iteration order is otherwise arbitrary (and would
  // reshuffle every render besides).
  vector<string> library_paths;
  library_paths.reserve(instrument_provider.getTaxonomyPaths().size());
  for (auto & [ path, instrument ] : instrument_provider.getTaxonomyPaths()) library_paths.push_back(path);
  sort(library_paths.begin(), library_paths.end());
  for (auto & path : library_paths) {
    outline_row_s row;
    row.level = 2;
    row.type = TrackType::INSTRUMENT;
    row.kind = OutlineRowKind::LIBRARY_INSTRUMENT;
    row.label = path;
    row.ref_name = path;
    data_.push_back(std::move(row));
  }

  if (song.getMajorVersion() != current_song_version_ || new_scroll_pos_ != current_scroll_pos_ || focused != current_focused_) {
    render_all = true;
  }

  auto tree_rows = treeRows();
  auto [tree_width, cols] = columnSplit();
  bool cursor_changed = new_cursor_row_ != current_cursor_row_;

  bool need_refresh = false;
  if (render_all) {
    current_scroll_pos_ = new_scroll_pos_;

    renderHeading(styles);
    for (int i = 0; i < tree_rows; i++) {
      renderRow(styles, i, focused && i == new_cursor_row_ - current_scroll_pos_);
    }
    renderDetailsPanel(styles, tree_width + 1, cols - tree_width - 1);
    need_refresh = true;
  } else if (cursor_changed) {
    renderRow(styles, current_cursor_row_ - current_scroll_pos_, false);
    renderRow(styles, new_cursor_row_ - current_scroll_pos_, focused);
    // The details panel's own content depends on which row the cursor is
    // now on (a different kind may show completely different actions, or
    // none) - always redrawn whole on a cursor move rather than tracking
    // a finer-grained diff, unlike renderRow()'s own incremental
    // old-row/new-row pair above.
    renderDetailsPanel(styles, tree_width + 1, cols - tree_width - 1);
    need_refresh = true;
  }

  current_song_version_ = song.getMajorVersion();
  current_cursor_row_ = new_cursor_row_;
  current_focused_ = focused;

  return need_refresh;
}

pair<int, int>
OutlineView::columnSplit() const {
  auto cols = getDim().second;
  // Capped to a third of the widget's own width, not just the fixed
  // kDetailsPanelWidth - a narrow terminal shrinks the details panel
  // rather than ever letting it crowd out the tree entirely.
  auto details_width = std::min(kDetailsPanelWidth, cols / 3);
  auto tree_width = std::max(0, cols - details_width - 1); // -1: the divider column between them
  return { tree_width, cols };
}

int
OutlineView::treeRows() const {
  return std::max(0, getDim().first - 1); // minus the one shared heading row
}

void
OutlineView::renderHeading(const StyleProvider & styles) {
  auto [tree_width, cols] = columnSplit();
  auto rows = getDim().first;

  setFgColor(styles.window_accent_fg_color);
  setBgColor(styles.window_accent_bg_color);
  putstr(0, 0, string(static_cast<size_t>(std::max(0, tree_width)), ' '));
  putstr(0, 1, "Outline");
  auto details_x = tree_width + 1;
  if (details_x < cols) {
    putstr(0, details_x, string(static_cast<size_t>(cols - details_x), ' '));
    putstr(0, details_x + 1, "Details");
  }

  // The divider between the tree and the details panel - static, so it
  // only ever needs (re)drawing here, alongside the heading it splits in
  // two - full height, row 0 (crossing through the heading itself)
  // included.
  setFgColor(styles.window_border_color);
  setBgColor(styles.window_bg_color);
  for (int row = 0; row < rows; row++) putstr(row, tree_width, "│");
}

void
OutlineView::renderDetailsPanel(const StyleProvider & styles, int details_x, int details_width) {
  if (details_width <= 0) return; // a pathologically narrow screen rect - nothing to draw

  auto tree_rows = treeRows();
  setFgColor(styles.window_fg_color);
  setBgColor(styles.window_bg_color);
  string blank(static_cast<size_t>(details_width), ' ');
  for (int row = 0; row < tree_rows; row++) putstr(1 + row, details_x, blank);

  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  auto lines = buildDetailsLines(data_[static_cast<size_t>(new_cursor_row_)], details_width);

  for (size_t i = 0; i < lines.size() && static_cast<int>(i) < tree_rows; i++) {
    auto & line = lines[i];
    auto truncated = Utf8::truncateToWidth(line.text, details_width);
    // Every button's own text is "[Key] Label" - only the "[Key]" part
    // gets the colored chip, so it reads as the pressable key rather than
    // coloring the whole label; the rest stays plain text like any other
    // line. All button labels are plain ASCII, so a byte-index find of
    // ']' is safe here (unlike wrapped description text elsewhere, which
    // isn't).
    auto bracket_end = line.action != DetailsAction::NONE ? truncated.find(']') : string::npos;
    if (bracket_end != string::npos) {
      auto key_part = truncated.substr(0, bracket_end + 1);
      auto rest_part = truncated.substr(bracket_end + 1);
      setFgColor(styles.button_fg_color);
      setBgColor(styles.button_bg_color);
      putstr(1 + static_cast<int>(i), details_x, key_part);
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
      putstr(1 + static_cast<int>(i), details_x + Utf8::displayWidth(key_part), rest_part);
    } else {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
      putstr(1 + static_cast<int>(i), details_x, truncated);
    }
  }
}

vector<DetailsLine>
OutlineView::buildDetailsLines(const outline_row_s & row, int details_width) const {
  vector<DetailsLine> lines;
  switch (row.kind) {
  case OutlineRowKind::TRACK:
  case OutlineRowKind::POOL_INSTRUMENT:
    lines.push_back({ "[Del] Delete", DetailsAction::DELETE });
    break;
  case OutlineRowKind::LIBRARY_INSTRUMENT:
    lines.push_back({ "[Enter] Add to Song", DetailsAction::ADD_TO_SONG });
    lines.push_back({ "[a] Stop", DetailsAction::STOP });
    lines.push_back({ "", DetailsAction::NONE });
    lines.push_back({ "Play note keys to preview", DetailsAction::NONE });
    if (auto * description = findGmInstrumentDescription(row.ref_name)) {
      lines.push_back({ "", DetailsAction::NONE });
      for (auto & wrapped : wrapText(description, details_width)) lines.push_back({ wrapped, DetailsAction::NONE });
    }
    break;
  case OutlineRowKind::LIBRARY_GROOVE: {
    lines.push_back({ "[Enter] Add to Song", DetailsAction::ADD_TO_SONG });
    lines.push_back({ "[p] Preview", DetailsAction::PREVIEW });
    lines.push_back({ "[a] Stop", DetailsAction::STOP });
    if (auto * pattern = findGroovePattern(row.ref_name)) {
      lines.push_back({ "", DetailsAction::NONE });
      for (auto & wrapped : wrapText(pattern->description, details_width)) lines.push_back({ wrapped, DetailsAction::NONE });
    }
    break;
  }
  case OutlineRowKind::SECTION:
    break;
  }
  return lines;
}

void
OutlineView::renderRow(const StyleProvider & styles, int display_row, bool highlight) {
  auto tree_rows = treeRows();
  auto tree_width = columnSplit().first;

  if (display_row >= 0 && display_row < tree_rows) {
    if (highlight) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
    } else {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
    }

    string padding(static_cast<size_t>(std::max(0, tree_width)), ' ');
    // +1: row 0 of this widget's own screen rect is the shared heading
    // strip (renderHeading()), so the tree itself starts one row down.
    putstr(1 + display_row, 0, padding);

    auto data_row = static_cast<size_t>(display_row + current_scroll_pos_);
    if (data_row < data_.size()) {
      auto & data = data_[data_row];

      putstr(1 + display_row, data.level * 3, data.label);
    }
  }
}

void
OutlineView::moveCursorBy(int delta) {
  auto tree_rows = treeRows();

  new_cursor_row_ = std::clamp(new_cursor_row_ + delta, 0, std::max(0, static_cast<int>(data_.size()) - 1));
  if (new_cursor_row_ < new_scroll_pos_) new_scroll_pos_ = new_cursor_row_;
  if (new_cursor_row_ >= new_scroll_pos_ + tree_rows) new_scroll_pos_ = new_cursor_row_ - tree_rows + 1;
}

void
OutlineView::scrollBy(int delta) {
  // Never touches new_cursor_row_ - see this method's own doc comment on
  // OutlineView.h for why that's deliberate. Clamped so the view can
  // never scroll past the point where the last row is already fully
  // visible at the bottom.
  auto max_scroll = std::max(0, static_cast<int>(data_.size()) - treeRows());
  new_scroll_pos_ = std::clamp(new_scroll_pos_ + delta, 0, max_scroll);
}

void
OutlineView::addSelectedLibraryInstrumentToPool() {
  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  auto & row = data_[static_cast<size_t>(new_cursor_row_)];
  if (row.kind != OutlineRowKind::LIBRARY_INSTRUMENT) return;

  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom(row.ref_name);
  // Resolved right away (not left for the next song load) so it's
  // immediately usable - matches Song::open()'s own load-then-prepare
  // contract for every other pool entry (see GenericInstrument::prepare()'s
  // own doc comment).
  instrument->prepare(getController().getInstrumentProvider());
  getController().getSong().addInstrument(std::move(instrument));
}

void
OutlineView::addSelectedLibraryGrooveToSong() {
  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  auto & row = data_[static_cast<size_t>(new_cursor_row_)];
  if (row.kind != OutlineRowKind::LIBRARY_GROOVE) return;
  auto * pattern = findGroovePattern(row.ref_name);
  if (!pattern) return;

  auto & song = getController().getSong();

  // The song's first root PercussionTrack (matching this view's own
  // shallow, root-only Tracks listing above) - created fresh if none
  // exists yet, the same plain default-constructed PercussionTrack
  // "add-percussion-track" (PatternEditor.cpp) itself creates.
  Track * percussion_track = nullptr;
  for (auto & track : song.getMasterTrack().getChildren()) {
    if (track->getType() == TrackType::PERCUSSION_CONTROL) {
      percussion_track = track.get();
      break;
    }
  }
  if (!percussion_track) percussion_track = &song.addTrack(make_unique<PercussionTrack>());

  Clip clip(percussion_track->getInternalId());
  clip.setName(pattern->name);
  clip.setLength(pattern->length);
  auto & leaf_pattern = clip.getLeafPattern();
  // Hits landing on the same row become successive note columns - this
  // engine's own polyphony convention (a chord/simultaneous drum hits are
  // never one Note, they're several notes in the same row's own distinct
  // columns), exactly how two real, simultaneously-authored notes already
  // coexist in any ordinary pattern.
  unordered_map<int, int> next_column_by_row;
  for (auto & hit : pattern->hits) {
    auto & column = next_column_by_row[hit.row];
    leaf_pattern.setNote(hit.row, column, Note(hit.note, hit.velocity));
    column++;
  }
  song.addClip(std::move(clip));
}

void
OutlineView::deleteSelectedRow() {
  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  auto & row = data_[static_cast<size_t>(new_cursor_row_)];
  auto & song = getController().getSong();

  if (row.kind == OutlineRowKind::TRACK) {
    // Never remove the last remaining root track - render() and several
    // sibling call sites elsewhere in the UI index a root-track list with
    // no bounds check at all on the assumption at least one always exists
    // (docs/known_bugs.md's zero-root-tracks entry) - same floor
    // PatternEditor's own "delete-track" command holds.
    if (song.getRootTrackIds().size() <= 1) return;
    if (getController().getRecordingTrackId() == row.ref_id) getController().setRecordingTrackId(0);
    song.removeTrack(row.ref_id);
  } else if (row.kind == OutlineRowKind::POOL_INSTRUMENT) {
    song.removeInstrument(row.ref_id);
  }
}

void
OutlineView::runDetailsAction(DetailsAction action) {
  switch (action) {
  case DetailsAction::NONE:
    break;
  case DetailsAction::DELETE:
    deleteSelectedRow();
    break;
  case DetailsAction::ADD_TO_SONG:
    // Exactly one of these actually does anything, depending on which
    // kind of Library row the cursor is on - both no-op harmlessly
    // otherwise (see each one's own guard).
    addSelectedLibraryInstrumentToPool();
    addSelectedLibraryGrooveToSong();
    break;
  case DetailsAction::PREVIEW:
    // Only a Library > Grooves row actually has a PREVIEW action to run
    // (see buildDetailsLines()) - a Library > Instruments row's own
    // preview is driven by note keys instead, not this action, so
    // there's nothing to guard against here beyond the row still
    // actually being a groove.
    if (new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size()) &&
        data_[static_cast<size_t>(new_cursor_row_)].kind == OutlineRowKind::LIBRARY_GROOVE) {
      auto & name = data_[static_cast<size_t>(new_cursor_row_)].ref_name;
      getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PREVIEW_GROOVE, name));
    }
    break;
  case DetailsAction::STOP:
    held_preview_key_ = -1;
    getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PREVIEW_STOP));
    break;
  }
}

bool
OutlineView::handleClick(const InputEvent & input) {
  // Resolved on release only, matching SpinBox's own click convention -
  // PRESS is consumed (returns true, so it never falls through to
  // anything else) but otherwise a no-op.
  if (input.getKind() != InputEvent::Kind::RELEASE) return true;

  auto [pos_y, pos_x] = getPosition();
  auto [rows, cols] = getDim();
  auto y = input.getY() - pos_y, x = input.getX() - pos_x;
  if (y < 0 || y >= rows || x < 0 || x >= cols) return true; // shouldn't happen - only reached while this is the click's own target
  if (y == 0) return true; // the shared heading row - nothing clickable there

  auto content_row = y - 1; // 0-based row within the tree/details content area, below the heading
  auto tree_width = columnSplit().first;

  if (x < tree_width) {
    // A tree click - move the cursor straight to whichever row is
    // showing there. The clicked row is already on screen by definition,
    // so this never needs to touch new_scroll_pos_ the way moveCursorBy()
    // does.
    auto data_row = content_row + current_scroll_pos_;
    if (data_row >= 0 && data_row < static_cast<int>(data_.size())) new_cursor_row_ = data_row;
  } else if (x > tree_width && new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size())) {
    // A details-panel click - hit-test against the exact same lines
    // renderDetailsPanel() just drew there (buildDetailsLines() is the
    // one shared source for both), and run whichever line's own action.
    auto details_width = std::max(0, cols - tree_width - 1);
    auto lines = buildDetailsLines(data_[static_cast<size_t>(new_cursor_row_)], details_width);
    if (content_row >= 0 && content_row < static_cast<int>(lines.size())) {
      runDetailsAction(lines[static_cast<size_t>(content_row)].action);
    }
  }
  return true;
}

bool
OutlineView::offerInput(const InputEvent & input) {
  // Handled first, before even RELEASE's own early-return just below (a
  // mouse-button release would otherwise be swallowed there, since it's
  // never the held preview-note key) - see handleClick()'s own comment
  // for why it resolves on release anyway.
  if (input.getId() == NCKEY_BUTTON1) return handleClick(input);
  // A held preview-note key's RELEASE is handled once, up front, exactly
  // like PatternEditor's own live note entry (see its own comment on why
  // RELEASE needs this special early-return treatment) - it either matches
  // the one key currently previewing (fully handled) or is inert.
  if (input.getKind() == InputEvent::Kind::RELEASE) {
    if (input.getId() != held_preview_key_) return false;
    held_preview_key_ = -1;
    getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PREVIEW_STOP));
    return true;
  }

  if (input.getId() == NCKEY_UP) {
    moveCursorBy(-1);
    return true;
  } else if (input.getId() == NCKEY_DOWN) {
    moveCursorBy(1);
    return true;
  } else if (input.getId() == NCKEY_BUTTON4) { // scroll wheel up - see scrollBy()'s own comment for why this isn't moveCursorBy()
    scrollBy(-1);
    return true;
  } else if (input.getId() == NCKEY_BUTTON5) { // scroll wheel down
    scrollBy(1);
    return true;
  } else if (input.getId() == NCKEY_PGUP) {
    moveCursorBy(-treeRows());
    return true;
  } else if (input.getId() == NCKEY_PGDOWN) {
    moveCursorBy(treeRows());
    return true;
  } else if (input.getId() == NCKEY_ENTER) {
    runDetailsAction(DetailsAction::ADD_TO_SONG);
    return true;
  } else if (input.getId() == NCKEY_DEL || input.getId() == NCKEY_BACKSPACE) {
    runDetailsAction(DetailsAction::DELETE);
    return true;
  } else if (input.getId() == 'p' && new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size()) &&
             data_[static_cast<size_t>(new_cursor_row_)].kind == OutlineRowKind::LIBRARY_GROOVE) {
    // Loops the groove under the cursor (PlaybackControlEvent::
    // PREVIEW_GROOVE) - 'p' rather than a note key, since a whole rhythm
    // pattern has no single pitch to bind to a keyboard note the way a
    // Library > Instruments row's audition does. Retriggering (pressing
    // 'p' again, even on the same row) restarts it cleanly from row 0 -
    // see Player.h's own preview_groove_pattern_ comment. 'a' (above)
    // stops it, the same universal stop every other preview already uses.
    if (input.getKind() == InputEvent::Kind::REPEAT) return true;
    runDetailsAction(DetailsAction::PREVIEW);
    return true;
  } else if (input.getId() == 'a') {
    // The dedicated note-off key (matches PatternEditor's own note-entry
    // convention - see its own is_off comment; 'a' is unmapped in
    // InputEvent::toMidiNote()'s pitched-note table for exactly this
    // reason, so stealing it here never shadows a real note). Always
    // available, not gated on the cursor still sitting on the library row
    // that started the preview - RELEASE alone (above) can't reliably
    // stop a sustained instrument, since a terminal with no Kitty
    // keyboard protocol never sends one at all (see held_preview_key_'s
    // own comment) - this is the one way to stop it that works
    // regardless. A harmless no-op if nothing is previewing.
    runDetailsAction(DetailsAction::STOP);
    return true;
  } else if (new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size()) &&
             data_[static_cast<size_t>(new_cursor_row_)].kind == OutlineRowKind::LIBRARY_INSTRUMENT) {
    // A held key's terminal-generated auto-repeat must not retrigger a
    // fresh preview note (holding a key should sustain the one already
    // sounding, not restart its envelope over and over) - mirrors
    // PatternEditor's own identical guard.
    if (input.getKind() == InputEvent::Kind::REPEAT) return true;

    auto midi_note = input.toMidiNote(getController().getGlobalOctave(), getController().getSong().getTuning());
    if (midi_note < 0) return false;

    auto & path = data_[static_cast<size_t>(new_cursor_row_)].ref_name;
    getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(
      PlaybackControlEvent::PREVIEW_NOTE, path, midi_note, constants::DEFAULT_VELOCITY));
    // A terminal with no Kitty keyboard protocol reports every keystroke
    // as InputEvent::Kind::UNKNOWN (see PatternEditor.cpp's own identical
    // comment) - there is no way to ever learn such a key was released, so
    // held_preview_key_ must stay untouched (a fresh PREVIEW_NOTE replaces
    // the sounding voice outright either way - see Player.h's own comment
    // on preview_note_voice_).
    if (input.getKind() != InputEvent::Kind::UNKNOWN) held_preview_key_ = input.getId();
    return true;
  }

  return false;
}
