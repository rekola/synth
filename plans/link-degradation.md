# Degrading gracefully on a slow link

Status: plan, revised after review. Awaiting approval; nothing here is built.
Builds on the terminal frame pacer (`plans/frame-pacing-terminal.md`), which
measures the link but changes nothing about what is drawn.

## Decisions made

- **Dials are never glyphs.** They are images in every class. They are cached, so a
  dial costs bytes only the first time a picture is shown.
- **One colour mode for the whole screen**, not per widget: `COLOUR`, `GREY`, `MONO`.
  It is applied where bytes are made (the cell encoder and the image encoder), so
  text, charts, meters and dials all follow it and no widget draws a grey version.
  It is also what a very old terminal gets.
- **Kitty and iTerm2 pictures go as greyscale PNG**, compressed by a small in-tree
  deflate encoder (no new dependency). If a terminal refuses the grey PNG, a few
  quantised colour levels are sent instead.
- **No label changes and no reduced-resolution pictures**, the heatmap included. Axis
  labels stay drawn into the image.
- **Sixel is fixed**: no whole-bounding-box resends, and a grey palette in grey mode.
- **Hover feedback may be lost at 15 Hz and below.** Hover effects are coming and
  will cost more, so they are a managed cost (below).
- **No `HZ_120` class.** The status string is the backend's own (`LinkStatus::description`),
  so a terminal shows its bandwidth and a window its real vsync rate. No bandwidth
  estimate for the window.
- **The API may change.**

## 1. Link status

```cpp
// cellui/LinkStatus.h
enum class LinkClass { FULL, HZ_30, HZ_15, HZ_5, HZ_1 };   // FULL: 60 Hz and up; the others the rate sustained

struct LinkStatus {
  LinkClass cls = LinkClass::FULL;
  std::string description;   // backend-specific, shown as is: "480 kB/s, RTT 38 ms" / "60 Hz vsync, 58 fps"
  bool pinned = false;       // set by the user, not measured
};

// cellui::Backend
virtual LinkStatus linkStatus() const { return {}; }
using LinkHandler = std::function<void(const LinkStatus &)>;   // when the class changes
virtual void setLinkHandler(LinkHandler) { }
```

`FULL` covers everything from 60 Hz up: nothing is cut between 120, 60 and 60-ish, so
those are one class. `TestBackend` returns a status the test sets
(`setLinkStatus()`), so the app can be tested per class without a pty.

### What the description says

- **Terminal:** the pacer's estimate and RTT, `"480 kB/s, RTT 38 ms"` (`"no cursor
  reports, 4 Hz"` for a terminal that answers none).
- **Window:** the real vsync rate of the monitor the window is on
  (`gdk_monitor_get_refresh_rate()`, in mHz) and the frame rate achieved over the last
  second: `"60 Hz vsync, 58 fps"`. A fractional rate shows as such (`59.94 Hz`).
- The status bar shows the description whenever the class is below `FULL` or pinned
  (`Link: 15 Hz, 480 kB/s, RTT 38 ms`), and nothing on a good link. A line in the
  window's status bar showing the vsync rate always is a one-line option if wanted.

### Classes

A terminal's class is the frame rate its bandwidth sustains for a **reference frame**:
the bytes of a typical full-demand frame, measured in phase 0, not guessed. A window's
class comes from its frame clock: the achieved frame interval against the refresh interval
(`gdk_frame_clock_get_refresh_info()`, frame timings), so a slow remote display
(X forwarding, VNC, Broadway) lands in a low class with no bandwidth figure.

    FULL    sustains 60 Hz and up
    HZ_30   30-60      HZ_15   15-30      HZ_5   5-15      HZ_1   under 5

An RTT over 250 ms also selects page scrolling (3.7), whatever the class.

### Stability

The estimate is noisy (524 and 589 kB/s for a true 520 in two runs):

- **Down fast:** two seconds below a threshold moves one class down.
- **Up slowly:** fifteen seconds above 1.5x the next threshold, and only after a **trial**
  (below). A feature must not come back just to overload the link again.
- No change while the pacer is in a stall, or in the first seconds after start-up.
- A static UI sends nothing, so the estimate goes stale: the class **holds**, it does not
  fall.

### Probing upward

With scopes off there is little to measure. A recovery enables the next feature for a
few seconds as a trial; the pacer's own queueing-delay rule judges it. A failed trial
waits longer before the next one.

### Pinning

`--link-class full|30|15|5|1|auto` and the command `set-link-class` (with a line in
`ui/CommandDescriptions.cpp`) pin the class; `auto` returns to measuring.

## 2. Colour mode

```cpp
enum class ColourMode { COLOUR, GREY, MONO };
```

Chosen by `--colours auto|colour|grey|mono` (and `CELLUI_COLORS`, which keeps its
existing values). `auto`:

- `MONO` for a terminal with no colour at all (`ColorDepth::NONE`);
- `GREY` for an 8- or 16-colour terminal (eight hues serve a theme worse than four
  greys do; `CELLUI_COLORS=8` forces the old mapping);
- `COLOUR` otherwise, and `GREY` on a slow link (`HZ_15` and below).

Applied in the backend, so no widget draws a grey version:

| Terminal | Levels | Sent as |
|---|---|---|
| truecolour | 256 | `38;2;g;g;g` / `48;2;g;g;g` |
| 256 colours | 26 | the xterm grey ramp (232-255) plus 16 and 231 |
| 8/16 colours | 4 | black, bright black, white, bright white |
| no colour (`MONO`) | 2 | the terminal's default; contrast by reverse video, bold and dim |

- **Conversion:** luminance of the colour, from sRGB, to the nearest level.
- **Contrast is the risk.** Hues that differ only in hue (the cyan cursor against the
  neutral grey bar/beat accents) collapse to one grey. So `Theme` gets a grey variant,
  chosen with the mode, whose semantic colours (cursor, selection, bar, beat, playhead,
  focus, error) are far enough apart in luminance; a test computes the pairs and fails
  when two that must differ are closer than a set number of levels. The encoder
  conversion stays as the safety net for colours the theme does not name.
- **`MONO`** needs the cell encoder to emit **reverse video** (`SGR 7`), which it does
  not today: with no colour a highlight is invisible now. A cell whose background is
  clearly lighter than its text on a dark theme (or the reverse on a light one) is drawn
  reversed; dim text uses `SGR 2`; emphasis uses bold.
- Images follow the mode (3.1, 3.2).

## 3. The steps

### 3.1 Compress, in tree (every class, no loss)

The biggest cost is chart pictures: raw RGB, base64, about 26 kB for an 80x80 picture, every
frame, in every protocol. Before cutting anything:

- A small **zlib-format deflate encoder** in `cellui/term/Deflate.{h,cpp}`: LZ77 with a
  hash chain and fixed Huffman codes (dynamic Huffman only if the measured ratio
  needs it), with Adler-32. Round-trip tested against a small inflate written for the
  tests, so no library is needed to test it.
- **kitty:** `o=z` on the payload; PNG (`f=100`) uses the same encoder for its IDAT
  stream, with a CRC-32 writer next to the PNG writer that exists (`encodePng()`, for
  iTerm2, which is stored uncompressed today).
- Ratio is measured on the real charts first. A line chart on a flat background should
  shrink many times; a smooth heatmap much less.

### 3.2 Greyscale and quantised pictures

When the mode is `GREY` (or `MONO`) every picture is converted to 8-bit grey at encode
time:

- **kitty:** PNG colour type 0 (greyscale), 8-bit, or 4-bit when quantised to 16 levels
  (half a byte per pixel before deflate), `f=100`. kitty's raw formats have no 1-byte
  form (`f=24` RGB, `f=32` RGBA), so a grey PNG is what makes greyscale real there.
  Whether every kitty-protocol terminal decodes grey PNGs is not known: the **error reply
  (`q=1`) is the test**. On an error the encoder switches that terminal to a few
  **quantised colour levels** (raw RGB with `o=z`, or a palette PNG) and remembers it.
- **iTerm2:** the same grey PNG in its inline-image sequence.
- **Sixel:** the format has no greyscale pixel type; a palette is free-form, so grey
  means 16 or 32 grey registers instead of the 240-colour cube, which cuts the colour
  passes per band, usually the bulk of a Sixel image. A monochrome sixel terminal takes
  HLS lightness definitions (`#n;1;0;l;0`), so the same palette works there.
- **`MONO`:** pictures are sent as grey as above where the terminal shows pictures at all;
  a terminal with no colour has no pictures and uses the cell charts.
- **The heatmap** keeps its axis labels in the image and its resolution. In grey, its
  brightness and saturation (a fixed hue at two variables) become the luminance of the
  result, so two cells can look alike where saturation differed; when the terminal has
  colour and only the link is slow, the quantised-colour mode keeps both and is the better
  choice for it.

### 3.3 Sixel: send only what changed

Today `ImageEncoder::sixel()` sends the **bounding box** of an image's visible rectangles,
again whenever anything about it changes. Fix:

1. **One sixel per visible rectangle**, not their union, so a popup over a chart no longer
   resends the covered pixels as transparency.
2. **Tile diff:** when only the picture's version changed, compare with the previous pixels
   (kept with the image already shown) in cell-aligned tiles, and send only the changed tiles
   merged into rectangles per row. A sixel starts at a cell origin, hence the alignment;
   a tile's last band is partial when the cell height is not a multiple of 6.
3. A changed tile is dithered with the same screen-anchored matrix as now
   (`Dither::origin_*`), so it matches its neighbours.

Tests (`ImageEncoderTests`): a popup over a chart sends only the visible rectangles; a
one-row change sends only the tiles it touches; an unchanged picture sends nothing; a
removed image sends nothing and the text overwrites it.

### 3.4 One upload per picture, many placements

Today each placed picture has its own key, and a key's first appearance is an upload, so two
dials with identical pixels are two uploads. For kitty the encoder becomes **content-
addressed**: an upload per distinct picture (a hash of its pixels and size), then `a=p`
for each place it is shown.

- A picture is **not deleted** when its last placement goes (`a=d,d=i` removes the
  placement; the data stays) while it is in the **cache**; the cache is bounded by bytes
  (a few MB) and evicts the least recently placed with `a=d,d=I`. Charts and other one-use
  pictures keep today's behaviour: deleted with their placement.
- Cached pictures are uploaded **lazily**, on first use, never all at start: on a slow link
  an up-front burst of every dial would itself be the problem.
- Sixel and iTerm2 have no stored images, so a cache buys nothing there; they resend per
  placement as now (with 3.3).

### 3.5 Dials

Dials stay images in every class (no glyph version, by decision). To make them cacheable:

- **The pointer is quantised** to the steps the Launchpad faders show, so there are only a
  few pictures per look. (`SendFader.h` today: unity down to -36 dB in 6 dB steps and
  "off": **8 positions**. Sixteen means 3 dB steps, which would change the fader steps too:
  to confirm.)
- **Four looks** per position: normal, pressed, hover, selected. `hover` is new (the look
  exists in the plan for the coming hover effects); all four are implemented now so they
  can be tested.
- **The letter is part of the picture** (`DialImage.cpp` draws it in the ring's gap), so
  the three dials (A, B, Main) are three sets: 8 x 4 x 3 = **96 pictures** at 8 positions,
  or 16 x 4 x 3 = **192** at 16. They would be 64 in all (16 x 4) only if the letter moves
  into a text cell, which changes the dial's layout. 192 small pictures are still well under
  1 MB in the terminal's memory, and compress to a few hundred bytes each.
- Dragging with Shift (0.3 dB) still sets the exact value; the pointer moves when it crosses
  a step. The typed value is shown as text while editing, as now.
- Tests: all four looks at every position render, are distinct where they should differ,
  keep a stable key for equal pixels, and are uploaded once however many times shown
  (kitty); the gallery shows the grid.

### 3.6 Hover is a cost

Hover effects (coming) are redraws triggered by the pointer, and so are bandwidth:
each is a frame, and pictures that change on hover are new pictures. So:

- **Mouse reporting** `?1003` (any motion) falls back to `?1002` (drag only) at `HZ_15` and
  below, and hover effects stop there (accepted).
- **Pointer-shape updates** (OSC 22) stop at `HZ_5`.
- A hover look reuses cached dial pictures (3.5), so hovering a dial costs a placement, not
  an upload.
- A new hover effect declares what it costs and whether it needs a picture; one that needs a
  new picture is a candidate to be turned off sooner.

### 3.7 App behaviour by class

Content decisions live in the app (`src/ui/PerformanceProfile.h`: `profileFor(LinkClass)`,
applied by `InteractiveUI` when the link handler fires).

| | FULL | HZ_30 | HZ_15 | HZ_5 | HZ_1 |
|---|---|---|---|---|---|
| Colour mode | as detected | as detected | **GREY** | GREY | GREY |
| Scopes (spectrum, heatmap) | every frame | 30 Hz | 15 Hz | **off** | off |
| Channel meter | every frame | every frame | 15 Hz | 5 Hz, peak only | off |
| Level meters (clip grid, pattern editor) | every frame | every frame | 15 Hz | 5 Hz | cell meter, last resort |
| Animations (smooth scroll, easing, pulses) | on | on | **off** | off | off |
| Pattern editor scrolling | line by line | line by line | line by line | **page** | page |
| Hover effects, any-motion mouse | on | on | **off** | off | off |
| Pointer-shape updates | on | on | on | off | off |
| Dials | images | images | images | images | images |

The table is the starting point; phase 0's numbers set the cuts.

- **Animations** use `InteractiveUI::setReducedMotion()`, which exists: smooth scrolling,
  easing, the clip slots' pulsing (a steady colour instead) and flashing (a steady marker).
- **Page scrolling** (`PatternEditor::ScrollMode { LINE, PAGE }`): when the cursor, or a
  playing column's line, would leave the visible rows, the view jumps one page (visible rows
  minus the scroll margins), so the screen repaints once per page instead of per row. In
  Live View each column's line (`keepTrackLinesVisible()`) uses the same rule. The arrangement
  grid pages sideways by bars. A scrolled screen changes every cell, so this matters even for
  text; its size is measured in phase 0.
- **Key-release events** from the kitty keyboard protocol stop at `HZ_5` if `KeyReader` and
  note entry can do without them (checked first).
- The glyph (cell) charts stay the fallback for a display with no pictures, and a last resort
  at `HZ_1` only.

## 4. Phases

0. **Measure.** Run the real app with playback and scopes through the shaped pty; log bytes
   per second by kind and by picture key once a second (`SYNTH_LINK_STATS=<file>`, like the
   existing UI stats). Gives the reference frame and each feature's cost. Output: the class
   thresholds and the cuts in 3.7.
1. **API and classes.** `LinkStatus`, `Backend::linkStatus()/setLinkHandler()`, terminal
   classification with hysteresis and trials, the description strings (terminal
   bandwidth/RTT, window vsync and fps), the status bar, `--link-class` and
   `set-link-class`, `TestBackend::setLinkStatus()`. Harness checks: the class at five
   bandwidths, no flapping on the 524/589 noise, a pinned class stays.
2. **Colour mode** (section 2): the encoder levels, `MONO` with reverse video, the grey
   theme and the contrast test, `--colours`, discovery defaults.
3. **Deflate, PNG and grey pictures** (3.1, 3.2): the encoder and its round-trip tests;
   bytes per frame per protocol before and after on the real charts; the kitty fallback on
   an error reply tested with the harness's emulated terminal answering one.
4. **Sixel** (3.3, and grey registers).
5. **Picture cache and dials** (3.4, 3.5): content-addressed uploads, retention and the byte
   cap, the quantised dial pictures and four looks.
6. **App profile** (3.6, 3.7): scope/meter rates and off, animations, page scrolling, hover,
   mouse reporting. UI tests per class with `TestBackend`; a harness run at 50 kB/s asserts
   the editor stays usable (key latency, fps).
7. **Window class** from the frame clock and vsync (section 1).

## 5. Decisions I need

1. **Dial positions:** the code has 8 (6 dB steps). You said 16. Sixteen needs 3 dB fader
   steps too. And the letter is in the picture: 96 or 192 pictures, or 64 if the letter moves
   to a text cell. Which?
2. **Default for 8/16-colour terminals:** grey (my recommendation), with `CELLUI_COLORS=8` for
   the old mapping?
3. **Grey on a slow link:** automatic at `HZ_15` and below, as in the table, or only when asked?
4. **Classes:** `FULL / 30 / 15 / 5 / 1` as above. Is `FULL` (60 and up) as one class right?

## 6. Risks

- **Flapping.** Hysteresis and trials mitigate it, but the estimate is noisy and content-
  dependent; the first version logs every class change.
- **Grey loses information.** The heatmap's two variables merge; hue-only distinctions
  collapse. The theme variant and the contrast test are the control.
- **Protocol support.** Grey and palette PNG, `o=z` and the PNG decode are standard but not
  tested on every terminal; the fallback is quantised colour, then today's raw RGB.
- **Cache memory in the terminal** is outside our control; the byte cap is conservative and
  eviction explicit.
- **Page scrolling** is a behaviour change; it is only on in the slow classes and can be
  pinned off.
