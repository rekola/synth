#ifndef _SCALE_H_
#define _SCALE_H_

#include <algorithm>
#include <array>
#include <span>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Gamut.h"
#include "Tuning.h"

// A song-wide scale, for the sequencer (Song::getScaleDegreesWindow()) and,
// through its gamut (scaleGamut()), the tuning of the song's notes; the
// Launchpad's keyboard ignores it. NONE is the explicit "no scale chosen"
// state, not a fifth real scale - getScaleDegreesWindow() falls back to the
// plain chromatic scale for it.
enum class Scale {
  NONE = 0,
  MAJOR,
  MINOR,
  OTONAL,
  UTONAL,
  DIAMOND7,
  // The seven Babylonian modes of the Pythagorean gamut, each a rotation of
  // the one seven-note pattern, named after the tuning text (descending
  // reading; docs/glossary.md). The church-mode name is the rotation.
  NIS_TUHRIM, // Locrian    C D♭ E♭ F G♭ A♭ B♭
  ISARTUM,    // Ionian     C D E F G A B (the gamut's "normal" scale)
  EMBUBUM,    // Dorian     C D E♭ F G A B♭
  NID_QABLIM, // Phrygian   C D♭ E♭ F G A♭ B♭
  QABLITUM,   // Lydian     C D E F♯ G A B
  KITMUM,     // Mixolydian C D E F G A B♭
  PITUM,      // Aeolian    C D E♭ F G A♭ B♭

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
  case Scale::DIAMOND7:
    return "diamond7";
  case Scale::NIS_TUHRIM:
    return "nistuhrim";
  case Scale::ISARTUM:
    return "isartum";
  case Scale::EMBUBUM:
    return "embubum";
  case Scale::NID_QABLIM:
    return "nidqablim";
  case Scale::QABLITUM:
    return "qablitum";
  case Scale::KITMUM:
    return "kitmum";
  case Scale::PITUM:
    return "pitum";
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
    case Scale::DIAMOND7:
      return "Diamond (7-limit)";
    case Scale::NIS_TUHRIM:
      return "Nīš tuḫrim";
    case Scale::ISARTUM:
      return "Išartum";
    case Scale::EMBUBUM:
      return "Embūbum";
    case Scale::NID_QABLIM:
      return "Nīd qablim";
    case Scale::QABLITUM:
      return "Qablītum";
    case Scale::KITMUM:
      return "Kitmum";
    case Scale::PITUM:
      return "Pītum";
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
  if (text == "diamond7") return Scale::DIAMOND7;
  if (text == "nistuhrim") return Scale::NIS_TUHRIM;
  if (text == "isartum") return Scale::ISARTUM;
  if (text == "embubum") return Scale::EMBUBUM;
  if (text == "nidqablim") return Scale::NID_QABLIM;
  if (text == "qablitum") return Scale::QABLITUM;
  if (text == "kitmum") return Scale::KITMUM;
  if (text == "pitum") return Scale::PITUM;
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

// Every scale names its gamut, and selecting the scale selects the gamut: major
// and minor the duodene, otonal and utonal the superparticular gamut, diamond7
// the diamond, the seven Babylonian modes the Pythagorean gamut. The maqamat and NONE
// have none: they are tempered and get no tuning corrections. The scale itself
// is a selection from its gamut. Otonal is the superparticular gamut's notes of
// its side (or of both) through round kJustScaleRound:
// 1/1 13/12 9/8 7/6 5/4 4/3 3/2 5/3 7/4 11/6 15/8 (C D♭ D D♯ E F G A A♯↑ C𝄫'
// B), harmonics of the note a fifth below the tonic and the like; utonal is its
// mirror image, 1/1 16/15 12/11 8/7 6/5 4/3 3/2 8/5 12/7 16/9 24/13 (C D♭↓ C𝄪
// E𝄫 E♭ F G A♭ B𝄫 B♭↓ B↓). The terms are Partch's, though his otonalities are
// chords, not these scales. Diamond7 is all thirteen notes of the tonality
// diamond. Each Babylonian mode is seven notes of the Pythagorean gamut,
// 3-limit and meant for 53-EDO only (scaleSupportsTuning()); išartum is the
// gamut's default. Major and minor
// are seven of the duodene's twelve.
constexpr int kJustScaleRound = 3;

// The Babylonian modes, by ascending ratio.
// C D♭ E♭ F G♭ A♭ B♭
inline constexpr std::array<Ratio, 7> kNisTuhrimRatios = {Ratio{1, 1}, Ratio{256, 243}, Ratio{32, 27}, Ratio{4, 3}, Ratio{1024, 729}, Ratio{128, 81}, Ratio{16, 9}};
// C D E F G A B
inline constexpr std::array<Ratio, 7> kIsartumRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{81, 64}, Ratio{4, 3}, Ratio{3, 2}, Ratio{27, 16}, Ratio{243, 128}};
// C D E♭ F G A B♭
inline constexpr std::array<Ratio, 7> kEmbubumRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{32, 27}, Ratio{4, 3}, Ratio{3, 2}, Ratio{27, 16}, Ratio{16, 9}};
// C D♭ E♭ F G A♭ B♭
inline constexpr std::array<Ratio, 7> kNidQablimRatios = {Ratio{1, 1}, Ratio{256, 243}, Ratio{32, 27}, Ratio{4, 3}, Ratio{3, 2}, Ratio{128, 81}, Ratio{16, 9}};
// C D E F♯ G A B
inline constexpr std::array<Ratio, 7> kQablitumRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{81, 64}, Ratio{729, 512}, Ratio{3, 2}, Ratio{27, 16}, Ratio{243, 128}};
// C D E F G A B♭
inline constexpr std::array<Ratio, 7> kKitmumRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{81, 64}, Ratio{4, 3}, Ratio{3, 2}, Ratio{27, 16}, Ratio{16, 9}};
// C D E♭ F G A♭ B♭
inline constexpr std::array<Ratio, 7> kPitumRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{32, 27}, Ratio{4, 3}, Ratio{3, 2}, Ratio{128, 81}, Ratio{16, 9}};
// C D E F G A B
inline constexpr std::array<Ratio, 7> kMajorRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{5, 4}, Ratio{4, 3}, Ratio{3, 2}, Ratio{5, 3}, Ratio{15, 8}};
// C D E♭ F G A♭ B♭
inline constexpr std::array<Ratio, 7> kMinorRatios = {Ratio{1, 1}, Ratio{9, 8}, Ratio{6, 5}, Ratio{4, 3}, Ratio{3, 2}, Ratio{8, 5}, Ratio{9, 5}};

// Every ratio of `only` must be a note of the gamut it selects from: a typo
// would otherwise just drop that note from the scale.
constexpr bool allInGamut(std::span<const Ratio> only, std::span<const GamutNote> table) {
  for (auto & ratio : only) {
    bool found = false;
    for (auto & note : table) found = found || note.ratio == ratio;
    if (!found) return false;
  }
  return true;
}
static_assert(allInGamut(kNisTuhrimRatios, gamut::kPythagorean));
static_assert(allInGamut(kIsartumRatios, gamut::kPythagorean));
static_assert(allInGamut(kEmbubumRatios, gamut::kPythagorean));
static_assert(allInGamut(kNidQablimRatios, gamut::kPythagorean));
static_assert(allInGamut(kQablitumRatios, gamut::kPythagorean));
static_assert(allInGamut(kKitmumRatios, gamut::kPythagorean));
static_assert(allInGamut(kPitumRatios, gamut::kPythagorean));
static_assert(allInGamut(kMajorRatios, gamut::kDuodene));
static_assert(allInGamut(kMinorRatios, gamut::kDuodene));

struct JustScale {
  std::span<const GamutNote> gamut;
  std::optional<GamutSide> side; // nullopt: every note of the gamut
  int round = 0;                 // the last round included
  // When not empty, exactly these ratios are the scale (side and round unused);
  // allInGamut() checks them against the gamut.
  std::span<const Ratio> only = {};

  bool contains(const GamutNote & note) const {
    if (!only.empty()) return std::find(only.begin(), only.end(), note.ratio) != only.end();
    return note.round <= round && (!side || note.side == GamutSide::BOTH || note.side == *side);
  }
};

static inline bool isBabylonianMode(Scale scale) {
  switch (scale) {
    case Scale::NIS_TUHRIM:
    case Scale::ISARTUM:
    case Scale::EMBUBUM:
    case Scale::NID_QABLIM:
    case Scale::QABLITUM:
    case Scale::KITMUM:
    case Scale::PITUM:
      return true;
    default:
      return false;
  }
}

// Whether a scale's notes mean what it says under a tuning. The Pythagorean
// gamut has 3-limit ratios no spelling reaches in 31-EDO (81/64 lies between
// its steps 10 and 11), so its scale is for 53-EDO alone.
static inline bool scaleSupportsTuning(Scale scale, Tuning tuning) {
  return !isBabylonianMode(scale) || tuning == Tuning::EDO53;
}

static inline std::optional<JustScale> justScale(Scale scale) {
  switch (scale) {
    case Scale::MAJOR:
      return JustScale{gamut::kDuodene, std::nullopt, 0, kMajorRatios};
    case Scale::MINOR:
      return JustScale{gamut::kDuodene, std::nullopt, 0, kMinorRatios};
    case Scale::OTONAL:
      return JustScale{gamut::kSuperparticular, GamutSide::OTONAL, kJustScaleRound};
    case Scale::UTONAL:
      return JustScale{gamut::kSuperparticular, GamutSide::UTONAL, kJustScaleRound};
    case Scale::DIAMOND7:
      return JustScale{gamut::kDiamond7, std::nullopt, 0};
    case Scale::NIS_TUHRIM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kNisTuhrimRatios};
    case Scale::ISARTUM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kIsartumRatios};
    case Scale::EMBUBUM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kEmbubumRatios};
    case Scale::NID_QABLIM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kNidQablimRatios};
    case Scale::QABLITUM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kQablitumRatios};
    case Scale::KITMUM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kKitmumRatios};
    case Scale::PITUM:
      return JustScale{gamut::kPythagorean, std::nullopt, 0, kPitumRatios};
    default:
      break;
  }
  return std::nullopt;
}

// The scale's gamut; empty when it has none (the song is tempered).
static inline std::span<const GamutNote> scaleGamut(Scale scale) {
  auto just = justScale(scale);
  return just ? just->gamut : std::span<const GamutNote>{};
}

// Indices into scaleGamut(scale) of the scale's notes, by ascending ratio.
// Empty for a scale with no gamut.
static inline std::vector<std::size_t> scaleGamutIndices(Scale scale) {
  std::vector<std::size_t> out;
  auto just = justScale(scale);
  if (!just) return out;
  for (std::size_t i = 0; i < just->gamut.size(); i++) {
    if (just->contains(just->gamut[i])) out.push_back(i);
  }
  std::sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) { return just->gamut[a].ratio < just->gamut[b].ratio; });
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
// A gamut scale's names are its gamut's spellings (major and minor have their
// own, the same as the duodene's), chosen to mean those ratios
// in both 31 and 53-EDO, except the Babylonian modes, whose E, A and B♭ carry
// 53-EDO arrows that 31-EDO ignores and so land on the wrong step there; 12 and 19-EDO take the nearest step (in 12-EDO E𝄫
// sounds identical to D, so two degrees can share a key). 31-EDO has one step
// for each pair of notes a syntonic comma apart and ignores the arrow; 53-EDO
// has a step for each.
static inline std::vector<std::string_view> scaleDegreeNames(Scale scale) {
  switch (scale) {
  case Scale::MAJOR: return { "C", "D", "E", "F", "G", "A", "B" };
  case Scale::MINOR: return { "C", "D", "E♭", "F", "G", "A♭", "B♭" };
  case Scale::OTONAL:
  case Scale::UTONAL:
  case Scale::DIAMOND7:
  case Scale::NIS_TUHRIM:
  case Scale::ISARTUM:
  case Scale::EMBUBUM:
  case Scale::NID_QABLIM:
  case Scale::QABLITUM:
  case Scale::KITMUM:
  case Scale::PITUM: {
    std::vector<std::string_view> names;
    auto just = justScale(scale);
    if (!just) return names;
    for (auto i : scaleGamutIndices(scale)) names.push_back(just->gamut[i].spelling);
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
