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
| `surface.base` | `#0d1115` | `#f0f4f7` | The window and the quietest areas |
| `surface.panel` | `#161a1e` | `#e2e7eb` | Panels: outline, scopes, clip slots, pattern editor |
| `surface.raised` | `#21262b` | `#cfd5db` | Headings, bar rows |
| `surface.overlay` | `#2d3238` | `#fcfeff` | Floating layers (the command candidates, dialogs) |
| `text.primary` | `#e7ecf0` | `#11171d` | Data, headings, the thing being read |
| `text.secondary` | `#acb2b7` | `#3a4148` | Labels, units, empty cells next to data |
| `text.disabled` | `#70757a` | `#5e646a` | Placeholders, off flags, unset cells (UI-glyph contrast) |
| `accent` | `#7acce0` | `#006185` | Cursor, region, focus, selected item |
| `accent.dim` | `#234951` | `#a2ccdd` | Unfocused cursor, selected row tone |
| `text.on-accent` | `#071417` | `#f7fbfd` | Text on the accent |

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
| 0 | 239 | 0.16 | `#0085cf` |
| 1 | 350 | 0.08 | `#a06882` |
| 2 | 105 | 0.16 | `#8b7f00` |
| 3 | 278 | 0.19 | `#6668e9` |
| 4 | 182 | 0.08 | `#3c8b7f` |
| 5 | 314 | 0.12 | `#9565ad` |
| 6 | 78 | 0.19 | `#b76600` |
| 7 | 272 | 0.04 | `#737b94` |

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
text.primary / base                15.91  need 4.5  ok
text.primary / raised              12.83  need 4.5  ok
text.secondary / base               8.84  need 4.5  ok
text.secondary / panel              8.16  need 4.5  ok
text.disabled / base (UI glyph)     4.08  need 3.0  ok
accent / base (UI glyph)           10.42  need 3.0  ok
text.on-accent / accent            10.30  need 4.5  ok
text.primary / accent.dim           8.26  need 4.5  ok
state.playing / base                4.90  need 3.0  ok
state.playing / panel               4.53  need 3.0  ok
state.playing.dim / base            4.64  need 3.0  ok
state.playing.dim / panel           4.28  need 3.0  ok
state.recording / base              4.33  need 3.0  ok
state.recording / panel             4.00  need 3.0  ok
state.recording.dim / base          4.36  need 3.0  ok
state.recording.dim / panel         4.03  need 3.0  ok
track 0 / base                      4.74  need 3.0  ok
track 0 / panel                     4.37  need 3.0  ok
track 1 / base                      4.33  need 3.0  ok
track 1 / panel                     4.00  need 3.0  ok
track 2 / base                      4.62  need 3.0  ok
track 2 / panel                     4.27  need 3.0  ok
track 3 / base                      4.26  need 3.0  ok
track 3 / panel                     3.94  need 3.0  ok
track 4 / base                      4.71  need 3.0  ok
track 4 / panel                     4.35  need 3.0  ok
track 5 / base                      4.27  need 3.0  ok
track 5 / panel                     3.95  need 3.0  ok
track 6 / base                      4.44  need 3.0  ok
track 6 / panel                     4.10  need 3.0  ok
track 7 / base                      4.50  need 3.0  ok
track 7 / panel                     4.15  need 3.0  ok
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
track 0 / base                      3.62  need 3.0  ok
track 0 / panel                     3.21  need 3.0  ok
track 1 / base                      3.96  need 3.0  ok
track 1 / panel                     3.51  need 3.0  ok
track 2 / base                      3.71  need 3.0  ok
track 2 / panel                     3.29  need 3.0  ok
track 3 / base                      4.02  need 3.0  ok
track 3 / panel                     3.57  need 3.0  ok
track 4 / base                      3.64  need 3.0  ok
track 4 / panel                     3.23  need 3.0  ok
track 5 / base                      4.01  need 3.0  ok
track 5 / panel                     3.56  need 3.0  ok
track 6 / base                      3.86  need 3.0  ok
track 6 / panel                     3.43  need 3.0  ok
track 7 / base                      3.81  need 3.0  ok
track 7 / panel                     3.38  need 3.0  ok
== shared (text on the colour)
text.on-track / state.playing       5.37  need 4.5  ok
text.on-track / state.playing.dim   5.09  need 4.5  ok
text.on-track / state.recording     4.75  need 4.5  ok
text.on-track / state.recording.dim  4.78  need 4.5  ok
text.on-track / track 0             5.19  need 4.5  ok
text.on-track / track 1             4.75  need 4.5  ok
text.on-track / track 2             5.07  need 4.5  ok
text.on-track / track 3             4.67  need 4.5  ok
text.on-track / track 4             5.16  need 4.5  ok
text.on-track / track 5             4.69  need 4.5  ok
text.on-track / track 6             4.87  need 4.5  ok
text.on-track / track 7             4.93  need 4.5  ok
== track hues, distance to the state hues (need >= 35 degrees)
track 0: hue  239.0  nearest state hue  94.0  ok
track 1: hue  350.0  nearest state hue  37.0  ok
track 2: hue  105.0  nearest state hue  40.0  ok
track 3: hue  278.0  nearest state hue 109.0  ok
track 4: hue  182.0  nearest state hue  37.0  ok
track 5: hue  314.0  nearest state hue  73.0  ok
track 6: hue   78.0  nearest state hue  51.0  ok
track 7: hue  272.0  nearest state hue 115.0  ok
== track colours, least difference between two (OKLab distance x100, need >= 7)
tracks 1 and 5:   7.3  ok
```

Not yet covered: a high-contrast theme (step 5), the heatmap and spectrum
colours, and the 256-colour fallback, which maps each token to the nearest
palette entry by perceptual distance (OKLab), not RGB.
