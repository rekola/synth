# Modern look: colour tokens

The values behind `plans/modern-look.md` step 2, mocked in
`plans/modern-look-mock.html`. They come from `tools/modern_look/tokens.py`,
which holds the OKLCH source, converts to sRGB and checks the contrast rules;
this file is written from its output by `tools/modern_look/tokens_doc.py`.
Change the values in `tokens.py` and re-run both. `Theme::dark()` and
`Theme::light()` (`lib/cellui/include/cellui/Theme.h`) hold these values;
today's look is kept as `Theme::original()`. Roles the tables do not list
(cursor tints, command and reader text, track flags, locators, buttons, the
spectrum) take values derived from them there.

## Per theme

Neutrals are slightly cool (hue 250). Surfaces step in lightness, not in
lines: base, panel, raised, overlay. The steps are about 0.04-0.05 of OKLCH
lightness in both themes, which is what keeps the areas distinguishable
without lines; the light theme was first built with steps of 0.03 and read as
low contrast.

| Token | Dark | Light | Used for |
|---|---|---|---|
| `surface.base` | `#080c0f` | `#f0f4f7` | The window and the quietest areas |
| `surface.panel` | `#151a1f` | `#e2e7eb` | Panels: outline, scopes, clip slots, pattern editor |
| `surface.raised` | `#252b30` | `#cfd5db` | Headings, bar rows |
| `surface.overlay` | `#363d43` | `#fcfeff` | Floating layers (the command candidates, dialogs) |
| `text.primary` | `#f0f4f7` | `#11171d` | Data, headings, the thing being read |
| `text.secondary` | `#bfc5ca` | `#3a4148` | Labels, units, empty cells next to data |
| `text.disabled` | `#82878c` | `#5e646a` | Placeholders, off flags, unset cells (UI-glyph contrast) |
| `accent` | `#75d4eb` | `#006185` | Cursor, region, focus, selected item |
| `accent.dim` | `#245560` | `#a2ccdd` | Unfocused cursor, selected row tone |
| `text.on-accent` | `#040f13` | `#f7fbfd` | Text on the accent |

## Shared with the Launchpad: one value in every theme

The Launchpad is always dark, has no icons, and has to show the same colours
as the screen, so a clip's colour and state cannot differ between a dark and a
light theme. These tokens therefore have **one value**. One lightness, OKLCH L
about 0.585-0.6, is at least 3:1 against the darkest surface and the light
theme's panel, so a single colour works on both.

| Token | Value | Meaning |
|---|---|---|
| `state.playing` | `#009722` | The fill of a whole clip slot while it is playing **or queued to play**, as the whole pad turns green. Playing pulses and queued flashes, on the pads and in the terminal alike (`statePulse()` in `ClipGrid.cpp`: a one-second pulse between the colour and a shade 60% darker, a 250 ms flash); the glyph (`▸`, `▹`) tells them apart where there is no motion. Hue 145, the Launchpad's green. |
| `state.playing.dim` | `#668666` | Launched while the transport is paused. Same hue, little chroma. |
| `state.recording` | `#e23532` | The fill of a slot while recording **or queued to record**. Hue 27, the Launchpad's red. |
| `state.recording.dim` | `#a46a64` | An armed track's empty slot, or a take queued to stop. |
| `text.on-track` | `#010204` | Text on a track or state colour, in both themes. |
| `data.heat` | `#e09036` | The DirAC heatmap's fixed hue (not a Launchpad matter, listed with the rest of the colours that have no light variant). |

There is deliberately no separate "queued" token: a queued clip uses the
playing colour (or the recording one), as it does on the pads. The hues are
those of the Launchpad's clip states; the exact RGB of its palette entries
(`LaunchpadManager.cpp`: green 21 and 23, red 5 and 7) is not in this
repository, so only the hue is matched here, and a pad's brightness is the
hardware's own.

### Track colours

The same eight in both themes. One lightness (OKLCH L 0.585); for each a hue and
a chroma, picked to make the least difference between any two as large as it
can be, on the screen and as a Launchpad pad shows it, with the hues kept
clear of the two state colours, so a track never looks like a playing or a
recording clip. A muted colour beside a vivid one tells close hues apart. The
order puts similar ones far apart in the ordinal, since neighbours are what sit
side by side. Tracks past the eighth step the hue by the golden ratio over the
free arcs (62-110 and 180-352 degrees, each 35 degrees from a state hue) at chroma 0.12.

| Track | Hue | Chroma | Colour |
|---|---|---|---|
| 0 | 205 | 0.14 | `#0092a3` |
| 1 | 344 | 0.08 | `#9e6887` |
| 2 | 108 | 0.19 | `#898100` |
| 3 | 263 | 0.12 | `#557ac3` |
| 4 | 108 | 0.04 | `#7e7e63` |
| 5 | 308 | 0.12 | `#9067b2` |
| 6 | 69 | 0.19 | `#c05e00` |
| 7 | 230 | 0.04 | `#64818f` |

Where a track colour is under text (a placed arrangement block, a clip
number) the text is `text.on-track`. The Launchpad takes the same colours,
quantized to what the pads can show.

The level meter ramp, quiet to loud, is the same in both themes too (it is a
green-to-red scale, separate from the clip states): `#2d8949`, `#56b93e`, `#e6ad00`, `#f76400`, `#e4312f`.

## Contrast, measured

Generated by `tools/modern_look/tokens.py`: WCAG contrast ratio of each pair,
against the stated minimum (4.5 body text, 3.0 UI glyphs and blocks). The
shared colours are checked against both themes' surfaces.

```
== dark
text.primary / base                17.78  need 4.5  ok
text.primary / raised              12.96  need 4.5  ok
text.secondary / base              11.27  need 4.5  ok
text.secondary / panel             10.04  need 4.5  ok
text.disabled / base (UI glyph)     5.41  need 3.0  ok
accent / base (UI glyph)           11.59  need 3.0  ok
text.on-accent / accent            11.41  need 4.5  ok
text.primary / accent.dim           7.49  need 4.5  ok
state.playing / base                5.08  need 3.0  ok
state.playing / panel               4.53  need 3.0  ok
state.playing.dim / base            4.81  need 3.0  ok
state.playing.dim / panel           4.28  need 3.0  ok
state.recording / base              4.49  need 3.0  ok
state.recording / panel             4.00  need 3.0  ok
state.recording.dim / base          4.53  need 3.0  ok
state.recording.dim / panel         4.03  need 3.0  ok
track 0 / base                      5.26  need 3.0  ok
track 0 / panel                     4.68  need 3.0  ok
track 1 / base                      4.49  need 3.0  ok
track 1 / panel                     4.00  need 3.0  ok
track 2 / base                      4.85  need 3.0  ok
track 2 / panel                     4.32  need 3.0  ok
track 3 / base                      4.63  need 3.0  ok
track 3 / panel                     4.12  need 3.0  ok
track 4 / base                      4.72  need 3.0  ok
track 4 / panel                     4.20  need 3.0  ok
track 5 / base                      4.45  need 3.0  ok
track 5 / panel                     3.96  need 3.0  ok
track 6 / base                      4.53  need 3.0  ok
track 6 / panel                     4.03  need 3.0  ok
track 7 / base                      4.74  need 3.0  ok
track 7 / panel                     4.22  need 3.0  ok
== light
text.primary / base                16.35  need 4.5  ok
text.primary / raised              12.22  need 4.5  ok
text.secondary / base               9.41  need 4.5  ok
text.secondary / panel              8.35  need 4.5  ok
text.disabled / base (UI glyph)     5.42  need 3.0  ok
accent / base (UI glyph)            6.27  need 3.0  ok
text.on-accent / accent             6.65  need 4.5  ok
text.primary / accent.dim          10.49  need 4.5  ok
state.playing / base                3.50  need 3.0  ok
state.playing / panel               3.10  need 3.0  ok
state.playing.dim / base            3.69  need 3.0  ok
state.playing.dim / panel           3.28  need 3.0  ok
state.recording / base              3.96  need 3.0  ok
state.recording / panel             3.51  need 3.0  ok
state.recording.dim / base          3.93  need 3.0  ok
state.recording.dim / panel         3.48  need 3.0  ok
track 0 / base                      3.38  need 3.0  ok
track 0 / panel                     3.00  need 3.0  ok
track 1 / base                      3.96  need 3.0  ok
track 1 / panel                     3.51  need 3.0  ok
track 2 / base                      3.67  need 3.0  ok
track 2 / panel                     3.25  need 3.0  ok
track 3 / base                      3.84  need 3.0  ok
track 3 / panel                     3.40  need 3.0  ok
track 4 / base                      3.77  need 3.0  ok
track 4 / panel                     3.34  need 3.0  ok
track 5 / base                      4.00  need 3.0  ok
track 5 / panel                     3.55  need 3.0  ok
track 6 / base                      3.92  need 3.0  ok
track 6 / panel                     3.48  need 3.0  ok
track 7 / base                      3.75  need 3.0  ok
track 7 / panel                     3.33  need 3.0  ok
== shared (text on the colour)
text.on-track / state.playing       5.37  need 4.5  ok
text.on-track / state.playing.dim   5.09  need 4.5  ok
text.on-track / state.recording     4.75  need 4.5  ok
text.on-track / state.recording.dim  4.78  need 4.5  ok
text.on-track / track 0             5.56  need 4.5  ok
text.on-track / track 1             4.75  need 4.5  ok
text.on-track / track 2             5.13  need 4.5  ok
text.on-track / track 3             4.90  need 4.5  ok
text.on-track / track 4             4.99  need 4.5  ok
text.on-track / track 5             4.70  need 4.5  ok
text.on-track / track 6             4.79  need 4.5  ok
text.on-track / track 7             5.01  need 4.5  ok
== track hues, distance to the state hues (need >= 35 degrees)
track 0: hue  205.0  nearest state hue  60.0  ok
track 1: hue  344.0  nearest state hue  43.0  ok
track 2: hue  108.0  nearest state hue  37.0  ok
track 3: hue  263.0  nearest state hue 118.0  ok
track 4: hue  108.0  nearest state hue  37.0  ok
track 5: hue  308.0  nearest state hue  79.0  ok
track 6: hue   69.0  nearest state hue  42.0  ok
track 7: hue  230.0  nearest state hue  85.0  ok
== track colours, least difference between two (OKLab distance x100, need >= 6.5)
tracks 4 and 7:   7.0  ok
```

Not yet covered: a high-contrast theme (step 5), the heatmap and spectrum
colours, and the 256-colour fallback, which maps each token to the nearest
palette entry by perceptual distance (OKLab), not RGB.
