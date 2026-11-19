#pragma once

// Editing a track's name in its header, shared by the clip grid and the
// pattern editor so the two look and behave alike. The header's label is the
// track's number and its name; the field covers the name only.

#include <cellui/InlineEditor.h>
#include "StyleProvider.h"
#include "../model/VisibleTrackInfo.h"

#include <algorithm>
#include <string>

namespace track_name_edit {

// "T<N>", then " name" if there is one. While the name is being edited only
// the number is drawn: the label is one proportional run, and what the field
// leaves of it is too short, so it would be cut with an ellipsis.
inline std::string label(int color_ordinal, const std::string & name, bool editing) {
  auto text = "T" + std::to_string(color_ordinal);
  if (!editing && !name.empty()) text += " " + name;
  return text;
}

// The field over the name part of a label that starts at (row, col) and is
// `label_cols` wide: the "T<N> " prefix stays.
inline cellui::InlineEditor::Field field(const StyleProvider & styles, const VisibleTrackInfo & info, const std::string & name, int row, int col, int label_cols) {
  auto prefix_width = std::min(static_cast<int>(label(info.color_ordinal_, "", false).size()) + 1, label_cols);
  cellui::InlineEditor::Field f;
  f.row = row;
  f.col = col + prefix_width;
  f.width = std::max(label_cols - prefix_width, 1);
  f.initial_text = name;
  f.text_color = styles.track_chips ? styles.window_accent_fg_color : styles.clip_text_color;
  f.backdrop = styles.track_chips ? styles.heading_bg_color : info.getColor(styles.tracks).blend(0.05f, Color(0, 0, 0));
  return f;
}

} // namespace track_name_edit
