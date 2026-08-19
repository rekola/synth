# Launchpad enharmonic-collision coloring

## Context

`LaunchpadLayout::computeConsonanceLevels()` colors every pitch class by
recursively splitting the octave the way classical interval theory does
(2/1 = 4/3 * 3/2, 3/2 = 6/5 * 5/4, ...), branding each depth-3+ landmark
"major-family" or "minor-family" depending on which side of that split it
fell on (`LaunchpadLayout.cpp`'s `Span::is_major`, hue offset by
`+-kDepth3HueOffset`/`+-kDepth4HueOffset` from `kRecursiveBaseHue`).

A coarse EDO doesn't have enough steps to keep every classical major/minor
pair distinct - both ratios round to the *same* pitch class. 12-EDO is the
obvious case (5-limit JI's major and minor whole tones, 9/8 and 10/9, both
round to 2 steps - "D" is simultaneously the major-family and minor-family
second), but it isn't unique to 12-EDO: 19-EDO collides the same pair at
its own step 3, and 31-EDO collides it at step 5 (still "D", still the
9/8-vs-10/9 pair, just at a different absolute step count) - exactly the
case in the request that prompted this plan. 53-EDO is fine enough that
none of the four pairs below collide at all.

Right now a colliding pitch class just silently gets whichever family's
landmark happened to reach it first in `computeConsonanceLevels()`'s
depth-order traversal (`mark()`'s `covered[]` check) - there is no signal
in the output that the note is actually ambiguous, and picking a "winner"
misrepresents it as cleanly belonging to one family. This plan adds a
dedicated classification for these pitch classes and colors them a
distinct reddish hue instead of major-blue or minor-violet.

**Rejected approach: mining the existing bisection tree for collisions.**
The obvious-looking alternative - watch `mark()`'s `covered[]` check for
attempts where the incoming family differs from whichever family already
claimed that pitch class - was tried by hand-simulating the algorithm
first (see below) and rejected. Restricted to depth-3 collisions only, it
finds the two pitch classes the existing
`compute_consonance_levels_12edo_ties_resolve_via_fifth_priority` test
already exercises for 12-EDO (3 and 9 - genuine cross-family ties, not
same-family duplicates as the test's own comment implies) but completely
misses the 31-EDO "D" case, whose conflict only shows up as a depth-4
landmark colliding with a depth-5 bisection from the opposite family.
Loosening the depth cutoff to catch that case (either side of the
collision at depth <=4, or attempted-depth within one level of the
existing landmark's depth) immediately overcorrects: 8/12, 9/19, 4/31,
and 1/53 pitch classes light up respectively for the narrower variant,
and past a third to half the keyboard for the looser ones - including
plain natural notes (D, E, A, B in 12-EDO) that have no business being
"ambiguous." The recursive bisection is a fine way to assign a hue that
*stays close to its harmonic neighborhood*, but its depth-N landmarks
past depth 3 have no independent musical identity (the file's own comment
on the level-4+ loop already says as much: "there's no further hand-named
next-generation ratio to rescale") - collisions between them are mostly
just numeric coincidences of a greedy tree covering a finite set of
integers, not real enharmonic equivalences.

## The fix: a direct ratio-collision check, run once per EDO

Reuse the pattern `Basis` already uses for thirds (`major_third`/
`minor_third`, 5/4 vs 6/5) and extend it to the other three diatonic
scale-degree pairs that classical 5-limit JI tunes differently for major
vs. minor: seconds (9/8 vs 10/9), sixths (5/3 vs 8/5), and sevenths (15/8
vs 9/5). For each pair, round both ratios to the nearest EDO step the same
way `computeBasis()` already rounds the third/fifth; if the two rounded
step counts coincide, that pitch class is the EDO's-worth-of-resolution
limit for that scale degree - flag it.

Hand-verified against all four supported EDOs (`round(edo_steps *
log2(ratio))` for each pair):

| EDO | 2nd (9/8 vs 10/9) | 3rd (5/4 vs 6/5) | 6th (5/3 vs 8/5) | 7th (15/8 vs 9/5) |
|-----|---|---|---|---|
| 12  | 2 vs 2 **collide** | 4 vs 3 | 9 vs 8 | 11 vs 10 |
| 19  | 3 vs 3 **collide** | 6 vs 5 | 14 vs 13 | 17 vs 16 |
| 31  | 5 vs 5 **collide** | 10 vs 8 | 23 vs 21 | 28 vs 26 |
| 53  | 9 vs 8 | 17 vs 14 | 39 vs 36 | 48 vs 45 |

Confirms the motivating example exactly (31-EDO's collision is on the
second, landing on step 5 - "D" in the key of C) and gives a small,
plausible-looking result set per EDO (one collision for 12/19/31-EDO,
none for 53-EDO) instead of the bisection-mining approach's dozens.

### `LaunchpadLayout.h`/`.cpp`

1. Add `major_second`/`minor_second`, `major_sixth`/`minor_sixth`,
   `major_seventh`/`minor_seventh` to `Basis`, computed in
   `computeBasis()` exactly like `major_third`/`minor_third` already are
   (`lround(edo_steps * log2(ratio))`). Keeping them as named `Basis`
   fields (rather than local constants inside `computeConsonanceLevels()`)
   matches how `major_third`/`minor_third` are already both public and
   independently unit-tested (`basis_major_and_minor_third_match_expected_step_counts`).

2. Add `PadTier::ENHARMONIC` to the `PadTier` enum, documented as: a pitch
   class where a major-family scale-degree ratio and its minor-family
   counterpart round to the identical EDO step, i.e. the tuning can't
   honestly keep the two apart.

3. In `computeConsonanceLevels()`, after the existing recursive-bisection
   pass finishes untouched (so the major/minor hue tree keeps its current
   behavior for every other pitch class), add a final overlay pass: for
   each of the four (major_step, minor_step) pairs, if they're equal mod
   `edo_steps` *and* that pitch class isn't already TONIC/FOURTH/FIFTH
   (the fixed, family-less landmarks - a real collision with one of those
   would need a hand-picked-EDO to happen at all, and family doesn't apply
   to them anyway), overwrite its `PadClassification` with
   `{PadTier::ENHARMONIC, kEnharmonicHue, <placeholder depth>}`. Iterate
   the four pairs in a fixed order for determinism in the (currently
   unobserved, but not provably impossible for some future EDO) case of
   more than one pair colliding on the same pitch class.
   - `depth` isn't meaningful for this tier (nothing recursive produced
     it) - give it a fixed placeholder (e.g. `3`) purely so the existing
     "`depth >= 1` means classified" invariant
     (`compute_consonance_levels_reaches_full_coverage_for_every_supported_edo`)
     keeps holding, and document that on `PadClassification::depth`.

### `LaunchpadManager.cpp`

4. `consonanceColor()`: add a branch for `PadTier::ENHARMONIC` ahead of
   the RECURSIVE fallback, returning a fixed reddish hue
   (`kConsonanceEnharmonicHue` - true red, hue ~0, is the natural choice;
   needs the same real-hardware confirmation this file's other hue/
   saturation constants already got, since red sits fairly close in hue
   to `kFourthFifthCenterHue`'s amber, ~18-24, even though the two read as
   distinct color names) at a saturation on par with `kConsonanceTonicSaturation`
   (this is meant to visually flag "heads up, ambiguous," so it should
   pop out at least as much as tonic, not blend in at RECURSIVE-tier
   saturation).

### Tests (`tests/LaunchpadLayoutTests.cpp`)

5. `basis_*` test for the three new `Basis` field pairs, mirroring
   `basis_major_and_minor_third_match_expected_step_counts` (table-driven
   over 12/19/31/53-EDO, expected values from the table above).

6. `compute_consonance_levels_flags_enharmonic_pitch_classes_where_seconds_collide`:
   asserts `levels[2]` (12-EDO), `levels[3]` (19-EDO), and `levels[5]`
   (31-EDO) are `PadTier::ENHARMONIC`, and that no pitch class in 53-EDO
   is `PadTier::ENHARMONIC` at all (guards against the feature ever
   firing where it shouldn't, on an EDO fine enough to keep every pair
   distinct).

7. Spot-check that the existing
   `compute_consonance_levels_12edo_ties_resolve_via_fifth_priority` test
   (pitch classes 3 and 9, both thirds-based ties) is unaffected - neither
   is the seconds-collision pitch class (2), so both should stay
   `PadTier::RECURSIVE`/depth 3 exactly as today. No code change needed
   for that test; just confirm it still passes once the overlay pass is
   in, since a bug in the new pass's TONIC/FOURTH/FIFTH exclusion could
   plausibly leak into unrelated pitch classes.

## Out of scope

- Re-deriving the bisection tree's own family split (depth 3+) to avoid
  the collision in the first place - the request asks for a new color on
  the colliding note, not a redesign of which note the split picks.
- Any change to `noteForPad`/`classifyPad`/the grid layout itself - this
  is purely a coloring addition, one more `PadTier` value consumed the
  same way the existing four already are.
