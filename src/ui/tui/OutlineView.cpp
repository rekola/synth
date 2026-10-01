#include "OutlineView.h"

#include "../../Controller.h"
#include "../../model/Song.h"
#include "../../model/SongStructure.h"
#include "../../model/PercussionTrack.h"
#include "../../model/GroovePatternLibrary.h"
#include "../../instruments/GenericInstrument.h"
#include "../../instruments/GmInstrumentDescriptions.h"
#include "../../instruments/Instrument.h"
#include "../../playback/InputEvent.h"
#include "../../playback/PlaybackControlEvent.h"
#include "../../util/constants.h"
#include "../../util/Utf8.h"
#include "../StyleProvider.h"

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

// Repeats a single-glyph UTF-8 string `count` display columns wide (the
// glyph is assumed to itself be exactly one display column, true of every
// glyph this is actually called with) - plain string(count, char)
// doesn't work here since the glyph is multiple bytes.
string repeatUtf8(const string & glyph, int count) {
  string result;
  result.reserve(glyph.size() * static_cast<size_t>(std::max(0, count)));
  for (int i = 0; i < count; i++) result += glyph;
  return result;
}

// The description shown for a Song > Instruments (pool) row - a custom
// authored one (Instrument::getDescription()) if present, else, for a
// GenericInstrument slot, whatever its resolved SoundFont/taxonomy entry
// says (GmInstrumentDescriptions.h, keyed by its own `from`); empty if
// neither applies (e.g. an Oscillator slot with nothing authored).
string poolInstrumentDescription(const Track * instrument) {
  if (auto * generic = dynamic_cast<const GenericInstrument *>(instrument)) {
    if (!generic->getDescription().empty()) return generic->getDescription();
    if (auto * inherited = findGmInstrumentDescription(generic->getFrom())) return inherited;
    return {};
  }
  if (auto * plain = dynamic_cast<const Instrument *>(instrument)) return plain->getDescription();
  return {};
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
  // count PatternEditor/ClipGrid/ArrangementGrid already show next to
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
  bool cursor_changed = new_cursor_row_ != current_cursor_row_;

  bool need_refresh = false;
  if (render_all) {
    current_scroll_pos_ = new_scroll_pos_;

    renderHeading(styles);
    for (int i = 0; i < tree_rows; i++) {
      renderRow(styles, i, i == new_cursor_row_ - current_scroll_pos_, focused);
    }
    renderButtonBar(styles);
    renderInfoPopup(styles);
    need_refresh = true;
  } else if (cursor_changed || details_dirty_) {
    if (cursor_changed) {
      renderRow(styles, current_cursor_row_ - current_scroll_pos_, false, focused);
      renderRow(styles, new_cursor_row_ - current_scroll_pos_, true, focused);
    }
    // The button bar and popup depend on which row the cursor is now on (a
    // different kind may show completely different actions, or none) -
    // always redrawn whole on a cursor move rather than tracking a
    // finer-grained diff, unlike renderRow()'s own incremental
    // old-row/new-row pair above. Also redrawn (with the cursor itself
    // untouched) whenever details_dirty_ says this row's own details
    // changed without moving the cursor at all - the target-track picker
    // committing a new choice, or the popup opening/closing.
    renderButtonBar(styles);
    renderInfoPopup(styles);
    need_refresh = true;
  }

  current_song_version_ = song.getMajorVersion();
  current_cursor_row_ = new_cursor_row_;
  current_focused_ = focused;
  details_dirty_ = false;

  return need_refresh;
}

int
OutlineView::treeRows() const {
  // Minus the heading row above, and the separator row plus the button
  // bar below.
  return std::max(0, getDim().first - kTreeTop - 1 - kButtonBarRows);
}

void
OutlineView::renderHeading(const StyleProvider & styles) {
  auto cols = getDim().second;

  setFgColor(styles.window_accent_fg_color);
  setBgColor(styles.heading_bg_color);
  putstr(0, 0, string(static_cast<size_t>(cols), ' '));
  putstr(0, 1, "Outline");
}

vector<DetailsLine>
OutlineView::infoLines(const outline_row_s & row) const {
  vector<DetailsLine> lines;
  for (auto & line : buildDetailsLines(row, kInfoPopupWidth - 4)) {
    if (line.action != DetailsAction::NONE) continue;
    if (line.text.empty() && lines.empty()) continue; // no leading blank line
    lines.push_back(line);
  }
  return lines;
}

vector<OutlineView::ButtonPlacement>
OutlineView::placeButtons(const outline_row_s & row) const {
  auto cols = getDim().second;
  vector<pair<DetailsAction, string>> buttons;
  for (auto & line : buildDetailsLines(row, cols)) {
    if (line.action != DetailsAction::NONE) buttons.push_back({ line.action, line.text });
  }
  if (!infoLines(row).empty()) buttons.push_back({ DetailsAction::TOGGLE_INFO, "[?] Info" });

  vector<ButtonPlacement> placed;
  int bar_row = 0, x = 0;
  for (auto & [ action, text ] : buttons) {
    auto text_width = std::min(Utf8::displayWidth(text), cols);
    if (x > 0 && x + text_width > cols) {
      bar_row++;
      x = 0;
    }
    if (bar_row >= kButtonBarRows) break;
    placed.push_back({ bar_row, x, action, Utf8::truncateToWidth(text, cols) });
    x += text_width + 1;
  }
  return placed;
}

void
OutlineView::renderButtonBar(const StyleProvider & styles) {
  auto cols = getDim().second;
  auto top = buttonBarTop();

  // The separator row between the tree and the bar.
  setFgColor(styles.window_border_color);
  setBgColor(styles.window_bg_color);
  putstr(top - 1, 0, repeatUtf8("─", cols));

  string blank(static_cast<size_t>(cols), ' ');
  setFgColor(styles.window_fg_color);
  for (int row = 0; row < kButtonBarRows; row++) putstr(top + row, 0, blank);

  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  for (auto & button : placeButtons(data_[static_cast<size_t>(new_cursor_row_)])) {
    // Every button's own text is "[Key] Label" - only the "[Key]" part
    // gets the colored chip, so it reads as the pressable key rather than
    // coloring the whole label. All button labels are plain ASCII, so a
    // byte-index find of ']' is safe here.
    auto bracket_end = button.text.find(']');
    auto key_part = bracket_end != string::npos ? button.text.substr(0, bracket_end + 1) : string();
    auto rest_part = button.text.substr(key_part.size());
    setFgColor(styles.button_fg_color);
    setBgColor(styles.button_bg_color);
    putstr(top + button.row, button.x, key_part);
    setFgColor(styles.window_fg_color);
    setBgColor(styles.window_bg_color);
    putstr(top + button.row, button.x + Utf8::displayWidth(key_part), rest_part);
  }
}

void
OutlineView::closeInfoPopup() {
  info_popup_open_ = false;
  info_popup_.reset();
}

void
OutlineView::renderInfoPopup(const StyleProvider & styles) {
  vector<DetailsLine> lines;
  if (info_popup_open_ && new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size())) {
    lines = infoLines(data_[static_cast<size_t>(new_cursor_row_)]);
  }
  if (lines.empty()) {
    info_popup_.reset();
    return;
  }

  // A plane of its own beside the panel, over whatever's to its right.
  // createChild() places a plane in screen coordinates, so it's moved
  // there explicitly.
  if (!info_popup_) info_popup_ = getPlane().createChild();
  auto [pos_y, pos_x] = getPosition();
  auto rows = static_cast<int>(lines.size()) + 2;
  auto width = kInfoPopupWidth;
  info_popup_->resize(rows, width);
  info_popup_->move(pos_y + kTreeTop, pos_x + getDim().second);
  info_popup_->moveToTop();

  auto bg = styles.window_accent_bg_color, fg = styles.window_fg_color, border = styles.window_border_color;
  info_popup_->setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
  info_popup_->setFgColor(border.getRed(), border.getGreen(), border.getBlue());
  info_popup_->putstr(0, 0, "┌─ Details " + repeatUtf8("─", width - 12) + "┐");
  for (int row = 1; row < rows - 1; row++) {
    info_popup_->putstr(row, 0, "│" + string(static_cast<size_t>(width - 2), ' ') + "│");
  }
  info_popup_->putstr(rows - 1, 0, "└" + repeatUtf8("─", width - 2) + "┘");
  info_popup_->setFgColor(fg.getRed(), fg.getGreen(), fg.getBlue());
  for (size_t i = 0; i < lines.size(); i++) {
    // One style at a time: each setter replaces the plane's whole style set.
    if (lines[i].style == DetailsStyle::TITLE) info_popup_->setBold(true);
    else if (lines[i].style == DetailsStyle::HINT) info_popup_->setItalic(true);
    else info_popup_->setBold(false);
    info_popup_->putstr(static_cast<int>(i) + 1, 2, Utf8::truncateToWidth(lines[i].text, width - 4));
  }
  info_popup_->setBold(false);
}

// The instrument's own name (a SoundFont's preset name, without the
// registry's namespace prefix) - empty when it has none.
string
OutlineView::libraryInstrumentName(const string & ref_name) const {
  auto & provider = getController().getInstrumentProvider();
  shared_ptr<Track> instrument = provider.tryGetByLiteralName(ref_name);
  if (!instrument) instrument = provider.resolvePath(ref_name);
  if (!instrument) return "";
  auto name = instrument->getName();
  constexpr string_view kNativePrefix = "native:";
  if (name.compare(0, kNativePrefix.size(), kNativePrefix) == 0) name = name.substr(kNativePrefix.size());
  return name;
}

vector<DetailsLine>
OutlineView::buildDetailsLines(const outline_row_s & row, int details_width) const {
  vector<DetailsLine> lines;
  switch (row.kind) {
  case OutlineRowKind::TRACK:
    lines.push_back({ "[Del] Delete", DetailsAction::DELETE });
    break;
  case OutlineRowKind::POOL_INSTRUMENT: {
    lines.push_back({ "[Del] Delete", DetailsAction::DELETE });
    lines.push_back({ "[a] Stop", DetailsAction::STOP });
    lines.push_back({ "", DetailsAction::NONE });
    lines.push_back({ row.label, DetailsAction::NONE, DetailsStyle::TITLE });
    lines.push_back({ "Play note keys to preview", DetailsAction::NONE, DetailsStyle::HINT });
    auto description = poolInstrumentDescription(getController().getSong().getInstrumentPool().getByIndex(row.ref_id));
    if (!description.empty()) {
      lines.push_back({ "", DetailsAction::NONE });
      for (auto & wrapped : wrapText(description, details_width)) lines.push_back({ wrapped, DetailsAction::NONE });
    }
    break;
  }
  case OutlineRowKind::LIBRARY_INSTRUMENT:
    lines.push_back({ "[Enter] Add to Song", DetailsAction::ADD_TO_SONG });
    lines.push_back({ "[a] Stop", DetailsAction::STOP });
    lines.push_back({ "", DetailsAction::NONE });
    {
      auto name = libraryInstrumentName(row.ref_name);
      lines.push_back({ name.empty() ? row.label : name, DetailsAction::NONE, DetailsStyle::TITLE });
    }
    lines.push_back({ "Play note keys to preview", DetailsAction::NONE, DetailsStyle::HINT });
    if (auto * description = findGmInstrumentDescription(row.ref_name)) {
      lines.push_back({ "", DetailsAction::NONE });
      for (auto & wrapped : wrapText(description, details_width)) lines.push_back({ wrapped, DetailsAction::NONE });
    }
    break;
  case OutlineRowKind::LIBRARY_GROOVE: {
    lines.push_back({ "[Enter] Add to Song", DetailsAction::ADD_TO_SONG });
    // Shows Add to Song's own current destination - 't' or a click opens
    // a real floating picker plane over this one to change it
    // (openTargetPicker()), always this row's own fixed screen position
    // (that method relies on it staying exactly the second line here).
    lines.push_back({ "[t] Target: " + targetTrackLabel(resolveTargetTrackId()), DetailsAction::TOGGLE_TARGET_PICKER });
    lines.push_back({ "[p] Preview", DetailsAction::PREVIEW });
    lines.push_back({ "[a] Stop", DetailsAction::STOP });
    lines.push_back({ row.label, DetailsAction::NONE, DetailsStyle::TITLE });
    if (auto * pattern = findGroovePattern(row.ref_name)) {
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
OutlineView::renderRow(const StyleProvider & styles, int display_row, bool cursor, bool focused) {
  auto tree_rows = treeRows();
  auto tree_width = getDim().second;

  if (display_row >= 0 && display_row < tree_rows) {
    if (cursor && focused) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
    } else if (cursor) {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.highlight_unfocused_bg_color);
    } else {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
    }

    string padding(static_cast<size_t>(std::max(0, tree_width)), ' ');
    putstr(kTreeTop + display_row, 0, padding);

    auto data_row = static_cast<size_t>(display_row + current_scroll_pos_);
    if (data_row < data_.size()) {
      auto & data = data_[data_row];

      putstr(kTreeTop + display_row, data.level * 3, data.label);
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

vector<const outline_row_s *>
OutlineView::compatibleTargetTrackRows() const {
  vector<const outline_row_s *> rows;
  for (auto & candidate : data_) {
    if (candidate.kind == OutlineRowKind::TRACK && candidate.type == TrackType::PERCUSSION_CONTROL) rows.push_back(&candidate);
  }
  return rows;
}

int
OutlineView::resolveTargetTrackId() const {
  if (selected_target_track_id_ == kNewTrackTargetId) return kNewTrackTargetId;
  for (auto * candidate : compatibleTargetTrackRows()) {
    if (candidate->ref_id == selected_target_track_id_) return selected_target_track_id_;
  }
  auto candidates = compatibleTargetTrackRows();
  return candidates.empty() ? kNewTrackTargetId : candidates.front()->ref_id;
}

string
OutlineView::targetTrackLabel(int track_id) const {
  if (track_id != kNewTrackTargetId) {
    for (auto & candidate : data_) {
      if (candidate.kind == OutlineRowKind::TRACK && candidate.ref_id == track_id) return candidate.label;
    }
  }
  return "New track";
}

void
OutlineView::openTargetPicker() {
  if (getPlane().pickerActive()) return;

  auto cols = getDim().second;
  auto candidates = compatibleTargetTrackRows();
  auto item_count = static_cast<int>(candidates.size()) + 1; // +1 for "New track"
  // +2 for ncselector's own top/bottom border. Opens just above the button
  // bar, so it never covers the button that opened it, capped so it never
  // reaches above the tree's first row.
  auto wanted_rows = item_count + 2;
  auto picker_rows = std::clamp(wanted_rows, 1, std::max(1, buttonBarTop() - 1 - kTreeTop));
  auto anchor_y = buttonBarTop() - 1 - picker_rows;

  getPlane().showPicker(anchor_y, 0, picker_rows, cols, item_count);
  for (auto * candidate : candidates) getPlane().addItem(candidate->label, "");
  getPlane().addItem("New track", "");

  // Starts highlighted on whatever's already the current choice, not
  // always row 0 - candidates were added in the same order as
  // compatibleTargetTrackRows() returns them, "New track" last (its own
  // index is candidates.size()).
  auto target_id = resolveTargetTrackId();
  auto default_index = static_cast<int>(candidates.size());
  for (size_t i = 0; i < candidates.size(); i++) {
    if (candidates[i]->ref_id == target_id) {
      default_index = static_cast<int>(i);
      break;
    }
  }
  getPlane().selectPickerItem(default_index);
}

void
OutlineView::closeTargetPicker() {
  getPlane().closePicker();
}

void
OutlineView::applyTargetPickerSelection(const string & selection) {
  if (selection.empty()) return; // nothing was ever highlighted - leave the previous choice untouched
  if (selection == "New track") {
    selected_target_track_id_ = kNewTrackTargetId;
    details_dirty_ = true; // the "[t] Target: ..." line's own text just changed with no cursor move to otherwise trigger a redraw
    return;
  }
  for (auto * candidate : compatibleTargetTrackRows()) {
    if (candidate->label == selection) {
      selected_target_track_id_ = candidate->ref_id;
      details_dirty_ = true;
      return;
    }
  }
}

void
OutlineView::addSelectedLibraryGrooveToSong() {
  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  auto & row = data_[static_cast<size_t>(new_cursor_row_)];
  if (row.kind != OutlineRowKind::LIBRARY_GROOVE) return;
  auto * pattern = findGroovePattern(row.ref_name);
  if (!pattern) return;

  auto & song = getController().getSong();

  // The target track picker's own current choice (see this class's own
  // header comment) - an existing root PercussionTrack, or a freshly
  // created one (the same plain default-constructed PercussionTrack
  // "add-percussion-track", PatternEditor.cpp, itself creates) if it
  // resolves to kNewTrackTargetId.
  auto target_id = resolveTargetTrackId();
  Track * percussion_track = nullptr;
  if (target_id != kNewTrackTargetId) {
    for (auto & track : song.getMasterTrack().getChildren()) {
      if (track->getInternalId() == target_id) {
        percussion_track = track.get();
        break;
      }
    }
  }
  if (!percussion_track) percussion_track = &song.addTrack(make_unique<PercussionTrack>());
  // Sticks the picker to whatever track actually got used - covers both
  // "resolved to kNewTrackTargetId because nothing existed yet" and "the
  // user explicitly picked New track" alike, so a second Add to Song
  // reuses this same fresh track instead of creating yet another one.
  selected_target_track_id_ = percussion_track->getInternalId();
  closeTargetPicker(); // a no-op unless Enter on Add to Song somehow ran while it was still open

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
  case DetailsAction::TOGGLE_TARGET_PICKER:
    if (getPlane().pickerActive()) closeTargetPicker(); else openTargetPicker();
    break;
  case DetailsAction::TOGGLE_INFO:
    info_popup_open_ = !info_popup_open_;
    details_dirty_ = true;
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
  if (y < kTreeTop) return true; // the heading row - nothing clickable there

  auto content_row = y - kTreeTop; // 0-based row within the tree
  auto bar_row = y - buttonBarTop();

  if (content_row < treeRows()) {
    // A tree click - move the cursor straight to whichever row is
    // showing there. The clicked row is already on screen by definition,
    // so this never needs to touch new_scroll_pos_ the way moveCursorBy()
    // does.
    auto data_row = content_row + current_scroll_pos_;
    if (data_row >= 0 && data_row < static_cast<int>(data_.size())) new_cursor_row_ = data_row;
  } else if (bar_row >= 0 && new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size())) {
    // A button-bar click - hit-test against the exact same placements
    // renderButtonBar() just drew, and run that button's action.
    for (auto & button : placeButtons(data_[static_cast<size_t>(new_cursor_row_)])) {
      if (button.row == bar_row && x >= button.x && x < button.x + Utf8::displayWidth(button.text)) {
        runDetailsAction(button.action);
        break;
      }
    }
  }
  return true;
}

bool
OutlineView::offerInput(const InputEvent & input) {
  // The Info popup is modal: it closes on its own keys or a click, and
  // swallows everything else. A key's release still gets through, to stop
  // a preview note held when it opened.
  if (info_popup_open_) {
    bool is_click = input.getId() == NCKEY_BUTTON1;
    bool closes = is_click ? input.getKind() == InputEvent::Kind::RELEASE
      : input.getKind() != InputEvent::Kind::RELEASE && (input.getId() == '?' || input.getId() == NCKEY_ESC || (input.hasCtrl() && input.getId() == 'g'));
    if (closes) {
      runDetailsAction(DetailsAction::TOGGLE_INFO);
      return true;
    }
    if (is_click || input.getKind() != InputEvent::Kind::RELEASE) return true;
  }

  // While the target-track picker is open, every keystroke/click goes to
  // it instead of anything below - mirrors PatternEditor::offerInput()'s
  // identical readerActive() handling. Enter and a click landing on an
  // item both commit (applyTargetPickerSelection() reads
  // getPickerSelection() before the plane is destroyed, since it has
  // nothing left to report once closed); Ctrl-g cancels without changing
  // the previous choice; everything else (arrow keys, scroll, PgUp/
  // PgDown) is just forwarded to the selector to navigate with, same as
  // ncselector_offer_input()'s own documented input set.
  if (getPlane().pickerActive()) {
    if (input.getId() == NCKEY_ENTER) {
      auto selection = getPlane().getPickerSelection();
      closeTargetPicker();
      applyTargetPickerSelection(selection);
      return true;
    } else if (input.hasCtrl() && input.getId() == 'g') {
      closeTargetPicker();
      return true;
    } else if (input.getId() == NCKEY_BUTTON1 && input.getKind() == InputEvent::Kind::RELEASE) {
      if (getPlane().offerInput(input)) {
        // Landed on an item (or the scroll arrows) - ncselector_offer_input()
        // only reports a click as relevant when it actually lands inside
        // its own plane. Commits whatever's now highlighted and closes,
        // the usual single-click-picks dropdown gesture - no separate
        // confirm step the way keyboard navigation needs Enter for.
        auto selection = getPlane().getPickerSelection();
        closeTargetPicker();
        applyTargetPickerSelection(selection);
      } else {
        // Landed outside the picker entirely - dismiss it without
        // changing the previous choice, same as clicking outside any
        // other dropdown/popup, then let the click fall through to
        // handleClick() as normal (it may be a perfectly ordinary click
        // on some other row/button).
        closeTargetPicker();
        return handleClick(input);
      }
      return true;
    } else {
      return getPlane().offerInput(input);
    }
  }

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

  if (input.getId() == '?') {
    // Not a note key (InputEvent::toMidiNote()), so it never shadows a
    // preview.
    runDetailsAction(DetailsAction::TOGGLE_INFO);
    return true;
  } else if (((input.hasCtrl() && input.getId() == 'g') || input.getId() == NCKEY_ESC) && info_popup_open_) {
    runDetailsAction(DetailsAction::TOGGLE_INFO);
    return true;
  } else if (input.getId() == NCKEY_UP) {
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
  } else if (input.getId() == 't' && new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size()) &&
             data_[static_cast<size_t>(new_cursor_row_)].kind == OutlineRowKind::LIBRARY_GROOVE) {
    // Opens the groove's own target-track picker (openTargetPicker()) - a
    // real floating plane, closed by picking a candidate, Enter, or
    // Ctrl-g (see this method's own pickerActive() handling up top).
    if (input.getKind() == InputEvent::Kind::REPEAT) return true;
    runDetailsAction(DetailsAction::TOGGLE_TARGET_PICKER);
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
             (data_[static_cast<size_t>(new_cursor_row_)].kind == OutlineRowKind::LIBRARY_INSTRUMENT ||
              data_[static_cast<size_t>(new_cursor_row_)].kind == OutlineRowKind::POOL_INSTRUMENT)) {
    // A held key's terminal-generated auto-repeat must not retrigger a
    // fresh preview note (holding a key should sustain the one already
    // sounding, not restart its envelope over and over) - mirrors
    // PatternEditor's own identical guard.
    if (input.getKind() == InputEvent::Kind::REPEAT) return true;

    auto midi_note = input.toMidiNote(getController().getGlobalOctave(), getController().getSong().getTuning());
    if (midi_note < 0) return false;

    auto & row = data_[static_cast<size_t>(new_cursor_row_)];
    if (row.kind == OutlineRowKind::LIBRARY_INSTRUMENT) {
      // Resolved here too (the same literal-name-then-taxonomy-path order
      // Player::handlePlaybackControlEvent() uses for the real PREVIEW_NOTE
      // handling) purely to prewarm it - see prewarmInstrumentForPreview()'s
      // own comment for why this must happen on this (UI) thread, before
      // the event reaches the audio thread, not just once at startup.
      auto & provider = getController().getInstrumentProvider();
      shared_ptr<Track> track_instrument = provider.tryGetByLiteralName(row.ref_name);
      if (!track_instrument) track_instrument = provider.resolvePath(row.ref_name);
      getController().prewarmInstrumentForPreview(track_instrument.get(), midi_note);
      getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(
        PlaybackControlEvent::PREVIEW_NOTE, row.ref_name, midi_note, constants::DEFAULT_VELOCITY));
    } else {
      // POOL_INSTRUMENT - addresses row.ref_id (the pool's own index) via
      // PREVIEW_POOL_NOTE rather than PREVIEW_NOTE, so the exact pool slot
      // (generator overrides/custom Oscillator parameters included) is
      // what sounds, not a fresh re-resolve of some name. Never
      // provider-registered, so it's never covered by the constructor-time
      // taxonomy-wide prewarm sweep at all - prewarming it here is the only
      // time it ever happens.
      auto song = getController().getCurrentSong();
      auto pool_instrument = song ? song->getInstrumentPool().getByIndex(row.ref_id) : nullptr;
      getController().prewarmInstrumentForPreview(pool_instrument, midi_note);
      getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(
        PlaybackControlEvent::PREVIEW_POOL_NOTE, "", row.ref_id, midi_note, constants::DEFAULT_VELOCITY));
    }
    // A terminal with no Kitty keyboard protocol reports every keystroke
    // as InputEvent::Kind::UNKNOWN (see PatternEditor.cpp's own identical
    // comment) - there is no way to ever learn such a key was released, so
    // held_preview_key_ must stay untouched (a fresh PREVIEW_NOTE/
    // PREVIEW_POOL_NOTE replaces the sounding voice outright either way -
    // see Player.h's own comment on preview_note_voice_).
    if (input.getKind() != InputEvent::Kind::UNKNOWN) held_preview_key_ = input.getId();
    return true;
  }

  return false;
}
