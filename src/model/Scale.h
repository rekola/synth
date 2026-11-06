#ifndef _SCALE_H_
#define _SCALE_H_

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Gamut.h"

// A song-wide scale, for the sequencer (Song::getScaleDegreesWindow()) and,
// for a just scale, which gamut notes it shows and prefers when the tuner has
// a choice; the Launchpad's keyboard ignores it. NONE is the explicit "no scale chosen"
// state, not a fifth real scale - getScaleDegreesWindow() falls back to the
// plain chromatic scale for it.
enum class Scale {
  NONE = 0,
  MAJOR,
  MINOR,
  OTONAL,
  UTONAL,

  // Arabic
  RAST,
  BAYATI,
  HIJAZ,
  NAHAWAND,
  KURD,
  AJAM,
  SABA,
  SIKAH
};

static inline std::string to_string(Scale scale) {
  switch (scale) {
  case Scale::NONE: return "";
  case Scale::MAJOR: return "major";
  case Scale::MINOR: return "minor";
  case Scale::OTONAL: return "otonal";
  case Scale::UTONAL: return "utonal";
  case Scale::RAST:
    return "rast";
  case Scale::BAYATI:
    return "bayati";
  case Scale::HIJAZ:
    return "hijaz";
  case Scale::NAHAWAND:
    return "nahawand";
  case Scale::KURD:
    return "kurd";
  case Scale::AJAM:
    return "ajam";
  case Scale::SABA:
    return "saba";
  case Scale::SIKAH:
    return "sikah";
  }
  return "";
}

// The name shown in menus and dialogs.
static inline std::string scaleLabel(Scale scale) {
  switch (scale) {
    case Scale::NONE:
      return "None (chromatic)";
    case Scale::MAJOR:
      return "Major";
    case Scale::MINOR:
      return "Minor";
    case Scale::OTONAL:
      return "Otonal";
    case Scale::UTONAL:
      return "Utonal";
    case Scale::RAST:
      return "Rāst";
    case Scale::BAYATI:
      return "Bayātī";
    case Scale::HIJAZ:
      return "Ḥijāz";
    case Scale::NAHAWAND:
      return "Nahāwand";
    case Scale::KURD:
      return "Kurd";
    case Scale::AJAM:
      return "ʿAjam";
    case Scale::SABA:
      return "Ṣabā";
    case Scale::SIKAH:
      return "Sīkāh";
  }
  return "";
}

static inline Scale scaleFromString(const std::string & text) {
  if (text == "major") return Scale::MAJOR;
  if (text == "minor") return Scale::MINOR;
  if (text == "otonal") return Scale::OTONAL;
  if (text == "utonal") return Scale::UTONAL;
  if (text == "rast") return Scale::RAST;
  if (text == "bayati") return Scale::BAYATI;
  if (text == "hijaz") return Scale::HIJAZ;
  if (text == "nahawand") return Scale::NAHAWAND;
  if (text == "kurd") return Scale::KURD;
  if (text == "ajam") return Scale::AJAM;
  if (text == "saba") return Scale::SABA;
  if (text == "sikah") return Scale::SIKAH;
  return Scale::NONE;
}

// The gamut notes a just scale selects: those of its side (or of both) up to
// round kJustScaleRound. Otonal is 1/1 13/12 9/8 7/6 5/4 4/3 3/2 5/3 7/4 11/6
// 15/8 (C D♭ D D♯ E F G A A♯↑ C𝄫' B), harmonics of the note a fifth below the
// tonic and the like; utonal is its mirror image, 1/1 16/15 12/11 8/7 6/5 4/3
// 3/2 8/5 12/7 16/9 24/13 (C D♭↓ C𝄪 E𝄫 E♭ F G A♭ B𝄫 B♭↓ B↓). The terms are
// Partch's, though his otonalities are chords, not these scales. nullopt for a
// scale that is not just.
constexpr int kJustScaleRound = 3;

static inline std::optional<GamutSide> scaleSide(Scale scale) {
  switch (scale) {
    case Scale::OTONAL:
      return GamutSide::OTONAL;
    case Scale::UTONAL:
      return GamutSide::UTONAL;
    default:
      break;
  }
  return std::nullopt;
}

static inline bool scaleContains(Scale scale, const GamutNote & note) {
  auto side = scaleSide(scale);
  return side && note.round <= kJustScaleRound && (note.side == GamutSide::BOTH || note.side == *side);
}

// Indices into gamut::kGamut of a just scale's notes, by ascending ratio.
// Empty for the others.
static inline std::vector<std::size_t> scaleGamutIndices(Scale scale) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < gamut::kGamut.size(); i++) {
    if (scaleContains(scale, gamut::kGamut[i])) out.push_back(i);
  }
  std::sort(out.begin(), out.end(), [](std::size_t a, std::size_t b) { return gamut::kGamut[a].ratio < gamut::kGamut[b].ratio; });
  return out;
}

// Each scale's own degrees, relative to C, in Note::stringToKey()'s own
// note-name syntax, resolved against the song's actual tuning/key there
// (Song::getScaleDegreesWindow()) rather than decoded into semitone numbers
// here, so the same degree list is correct under every supported tuning
// (12/19/31/53-EDO) instead of needing one hardcoded interval set per
// tuning. Empty for Scale::NONE: Song::getScaleDegreesWindow() reads that as
// "use the plain chromatic scale" and does not call this at all.
//
// A just scale's names are the gamut's spellings, chosen to mean those ratios
// in both 31 and 53-EDO; 12 and 19-EDO take the nearest step (in 12-EDO E𝄫
// sounds identical to D, so two degrees can share a key). 31-EDO has one step
// for each pair of notes a syntonic comma apart and ignores the arrow; 53-EDO
// has a step for each.
static inline std::vector<std::string_view> scaleDegreeNames(Scale scale) {
  switch (scale) {
  case Scale::MAJOR: return { "C", "D", "E", "F", "G", "A", "B" };
  case Scale::MINOR: return { "C", "D", "E♭", "F", "G", "A♭", "B♭" };
  case Scale::OTONAL:
  case Scale::UTONAL: {
    std::vector<std::string_view> names;
    for (auto i : scaleGamutIndices(scale)) names.push_back(gamut::kGamut[i].spelling);
    return names;
  }
  case Scale::RAST:
    return {"C", "D", "E𝄳", "F", "G", "A", "B𝄳"};
  case Scale::BAYATI:
    return {"C", "D𝄳", "E♭", "F", "G", "A♭", "B♭"};
  case Scale::HIJAZ:
    return {"C", "D♭", "E", "F", "G", "A♭", "B♭"};
  case Scale::NAHAWAND:
    return {"C", "D", "E♭", "F", "G", "A♭", "B♭"};
  case Scale::KURD:
    return {"C", "D♭", "E♭", "F", "G", "A♭", "B♭"};
  case Scale::AJAM:
    return {"C", "D", "E", "F", "G", "A", "B"};
  case Scale::SABA:
    return {"C", "D𝄳", "E♭", "F♭", "G", "A♭", "B♭"};
  case Scale::SIKAH:
    return {"C", "D𝄳", "E𝄳", "F𝄲", "G", "A𝄳", "B𝄳"};
  case Scale::NONE: return {};
  }
  return {};
}

#endif
