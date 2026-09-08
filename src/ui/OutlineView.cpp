#include "OutlineView.h"

#include "../Controller.h"
#include "../model/Song.h"
#include "../model/SongStructure.h"
#include "../instruments/GenericInstrument.h"
#include "../playback/InputEvent.h"
#include "../playback/PlaybackControlEvent.h"
#include "../util/constants.h"
#include "StyleProvider.h"

#include <algorithm>
#include <fmt/core.h>

using namespace std;

bool
OutlineView::render(const StyleProvider & styles, bool refresh, bool focused) {
  bool render_all = refresh;
  auto & song = getController().getSong();
  auto & instrument_provider = getController().getInstrumentProvider();

  data_.clear();
  data_.push_back( { 0, TrackType::MASTER, "Song" });
  data_.push_back( { 1, TrackType::UNKNOWN, "Instruments" });
  for (size_t i = 0; i < song.getInstrumentPool().getInstruments().size(); i++) {
    auto & instrument = *(song.getInstrumentPool().getInstruments()[i]);
    data_.push_back( { 2, TrackType::INSTRUMENT, instrument.getDisplayName() } );
  }
  data_.push_back( { 1, TrackType::UNKNOWN, "Tracks" });
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
    data_.push_back( { 2, track->getType(), label } );
  }
  data_.push_back( { 0, TrackType::UNKNOWN, "Library" });
  data_.push_back( { 1, TrackType::UNKNOWN, "Rhythms" });
  data_.push_back( { 1, TrackType::UNKNOWN, "Instruments" });
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
    data_.push_back( { 2, TrackType::INSTRUMENT, path, path } );
  }

  if (song.getMajorVersion() != current_song_version_ || new_scroll_pos_ != current_scroll_pos_ || focused != current_focused_) {
    render_all = true;
  }

  bool need_refresh = false;
  if (render_all) {
    current_scroll_pos_ = new_scroll_pos_;

    auto [rows, cols] = getDim();
    for (int i = 0; i < rows; i++) {
      renderRow(styles, i, focused && i == new_cursor_row_ - current_scroll_pos_);
    }
    need_refresh = true;
  } else if (new_cursor_row_ != current_cursor_row_) {
    renderRow(styles, current_cursor_row_ - current_scroll_pos_, false);
    renderRow(styles, new_cursor_row_ - current_scroll_pos_, focused);
    need_refresh = true;
  }

  current_song_version_ = song.getMajorVersion();
  current_cursor_row_ = new_cursor_row_;
  current_focused_ = focused;

  return need_refresh;
}

void
OutlineView::renderRow(const StyleProvider & styles, int display_row, bool highlight) {
  auto [rows, cols] = getDim();

  if (display_row >= 0 && display_row < rows) {
    if (highlight) {
      setFgColor(styles.highlight_fg_color);
      setBgColor(styles.highlight_bg_color);
    } else {
      setFgColor(styles.window_fg_color);
      setBgColor(styles.window_bg_color);
    }

    string padding(static_cast<size_t>(std::max(0, cols)), ' ');
    putstr(display_row, 0, padding);

    auto data_row = static_cast<size_t>(display_row + current_scroll_pos_);
    if (data_row < data_.size()) {
      auto & data = data_[data_row];

      putstr(display_row, data.level * 3, data.label);
    }
  }
}

void
OutlineView::moveCursorBy(int delta) {
  auto [rows, cols] = getDim();

  new_cursor_row_ = std::clamp(new_cursor_row_ + delta, 0, std::max(0, static_cast<int>(data_.size()) - 1));
  if (new_cursor_row_ < new_scroll_pos_) new_scroll_pos_ = new_cursor_row_;
  if (new_cursor_row_ >= new_scroll_pos_ + rows) new_scroll_pos_ = new_cursor_row_ - rows + 1;
}

void
OutlineView::addSelectedLibraryInstrumentToPool() {
  if (new_cursor_row_ < 0 || new_cursor_row_ >= static_cast<int>(data_.size())) return;
  auto & row = data_[static_cast<size_t>(new_cursor_row_)];
  if (row.library_instrument_path.empty()) return;

  auto instrument = make_unique<GenericInstrument>();
  instrument->setFrom(row.library_instrument_path);
  // Resolved right away (not left for the next song load) so it's
  // immediately usable - matches Song::open()'s own load-then-prepare
  // contract for every other pool entry (see GenericInstrument::prepare()'s
  // own doc comment).
  instrument->prepare(getController().getInstrumentProvider());
  getController().getSong().addInstrument(std::move(instrument));
}

bool
OutlineView::offerInput(const InputEvent & input) {
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
  } else if (input.getId() == NCKEY_PGUP) {
    moveCursorBy(-getDim().first);
    return true;
  } else if (input.getId() == NCKEY_PGDOWN) {
    moveCursorBy(getDim().first);
    return true;
  } else if (input.getId() == NCKEY_ENTER) {
    addSelectedLibraryInstrumentToPool();
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
    held_preview_key_ = -1;
    getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PREVIEW_STOP));
    return true;
  } else if (new_cursor_row_ >= 0 && new_cursor_row_ < static_cast<int>(data_.size()) &&
             !data_[static_cast<size_t>(new_cursor_row_)].library_instrument_path.empty()) {
    // A held key's terminal-generated auto-repeat must not retrigger a
    // fresh preview note (holding a key should sustain the one already
    // sounding, not restart its envelope over and over) - mirrors
    // PatternEditor's own identical guard.
    if (input.getKind() == InputEvent::Kind::REPEAT) return true;

    auto midi_note = input.toMidiNote(getController().getGlobalOctave(), getController().getSong().getTuning());
    if (midi_note < 0) return false;

    auto & path = data_[static_cast<size_t>(new_cursor_row_)].library_instrument_path;
    getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(
      PlaybackControlEvent::PREVIEW_NOTE, path, midi_note, constants::DEFAULT_VELOCITY));
    // A terminal with no Kitty keyboard protocol reports every keystroke
    // as InputEvent::Kind::UNKNOWN (see PatternEditor.cpp's own identical
    // comment) - there is no way to ever learn such a key was released, so
    // held_preview_key_ must stay untouched (a fresh PREVIEW_NOTE replaces
    // the sounding voice outright either way - see Player.h's own comment
    // on preview_voice_).
    if (input.getKind() != InputEvent::Kind::UNKNOWN) held_preview_key_ = input.getId();
    return true;
  }

  return false;
}
