# Degrading gracefully on a slow link

Status: plan for review. Builds on the terminal frame pacer
(`plans/frame-pacing-terminal.md`, PR #258), which measures the link but changes
nothing about what is drawn. Nothing here is built.

## Decisions already made

- **Dials are never drawn as glyphs.** They are mostly static: a picture with the
  same pixels keeps its key and is sent once, so they cost nothing on a slow link.
  No link class touches them.
- **Charts go greyscale before they go to glyphs.** The cell (glyph) versions stay
  a last resort, not a step on the way.
- **Lower resolution only for the heatmap**, and only once its axis labels are not
  part of the picture.
- **Sixel is fixed**, not avoided: it should not resend its whole bounding box.
- **The API may change** (a link status on `Backend`).
- **GTK gets a link status too**; a bandwidth estimate for it is wanted but not
  essential.

## 1. Link status

```cpp
// cellui/LinkStatus.h
enum class LinkClass { HZ_120, HZ_60, HZ_30, HZ_15, HZ_5, HZ_1 };   // the best frame rate the link sustains

struct LinkStatus {
  LinkClass cls = LinkClass::HZ_60;
  double bytes_per_sec = 0;   // 0: not known (a local window, or nothing sent yet)
  double fps = 0;             // frames actually shown over the last second
  double rtt_ms = 0;          // 0: not known
  bool pinned = false;        // set by the user, not measured
};

// cellui::Backend
virtual LinkStatus linkStatus() const { return {}; }
// Called on the UI thread when the class changes (not on every estimate).
using LinkHandler = std::function<void(const LinkStatus &)>;
virtual void setLinkHandler(LinkHandler) { }
```

`TestBackend` returns a status the test sets (`setLinkStatus()`), so the app's
behaviour per class can be tested without a pty.

### Classes

A class is the frame rate the link sustains for a **reference frame**: the bytes of
a typical full-demand frame (all scopes, meters, text), measured, not guessed
(phase 0 below). `HZ_120` and `HZ_60` only occur on a window (a terminal is paced to
60 by the 16 ms floor); `HZ_30` and below are bandwidth classes on a terminal.

    class      terminal: B / reference frame      window: frame clock
    HZ_120     (n/a)                              refresh >= 120 and frames on time
    HZ_60      >= 60                              refresh >= 60, on time
    HZ_30      >= 30                              frames late: achieved 30-60
    HZ_15      >= 15                              achieved 15-30
    HZ_5       >= 5                               achieved 5-15
    HZ_1       < 5                                achieved < 5

The reference frame's bytes are set after phase 0. RTT matters for how it feels,
not for the rate: a class at or below `HZ_5`, or an RTT over 250 ms, also selects
page scrolling (below).

### Stability

The estimate is noisy (524 and 589 kB/s for a true 520 in two runs), so:

- **Down fast:** two seconds below a threshold moves one class down (or straight to
  the class the budget says, below).
- **Up slowly:** fifteen seconds above 1.5x the next threshold, and only after a
  **trial** (see Probing upward). A dropped feature must not come back just to
  overload the link again.
- No class change while the pacer itself is in a stall or the first seconds after
  start-up.

### Cost budget

Classes give the default profile; a budget refines it. Every feature that costs
bandwidth reports its measured bytes per second (the encoder already splits a
frame into upload, placement and other; it will also attribute the upload bytes to
the picture key that caused them). The profile keeps the sum under 70% of B, so a
fast-enough link in a low class keeps what it can afford, and a feature is not
re-added if its measured cost would not fit.

### Probing upward

Once scopes are off or throttled there is little to measure: the pacer only learns
a higher rate from back-to-back frames. So a recovery needs a trial: enable the
next feature for a few seconds and see if the link keeps up (the pacer's own
queueing-delay rule judges it). A failed trial backs off and waits longer before
the next one.

### Pinning and display

- `--link-class 120|60|30|15|5|1|auto` and the command `set-link-class` (with a line
  in `ui/CommandDescriptions.cpp`) pin it; `auto` returns to measuring.
- The status bar shows `Link: 15 Hz, 480 kB/s` whenever the class is below the top
  one or pinned (`Link: 5 Hz (pinned)`); nothing otherwise, so a good link shows
  no clutter. Bandwidth is left out where it is not known (a local window).

### Terminal and window

The terminal's class comes from `FramePacer` (B, RTT, and what the app sends). The
window's comes from the frame clock: its refresh interval
(`gdk_frame_clock_get_refresh_info()`) and the frame interval it actually achieves
(frame timings), so a slow remote display (X forwarding, VNC, Broadway) lands in a
low class with no bandwidth number.

## 2. What each class changes

Content decisions live in the app (`src/ui/PerformanceProfile.h`:
`profileFor(LinkClass)`, applied by `InteractiveUI` when the link handler fires);
protocol decisions live in the terminal backend, which knows its class.

| | 120 / 60 | 30 | 15 | 5 | 1 |
|---|---|---|---|---|---|
| Scopes (spectrum, heatmap) | full colour, every frame | full colour, 30 Hz | **greyscale**, 15 Hz | **off** | off |
| Channel meter | every frame | every frame | 15 Hz | 5 Hz, peak only | off |
| Level meters (clip grid, pattern editor) | every frame | every frame | 15 Hz | 5 Hz | cell glyph meter (last resort) |
| Animations (smooth scroll, easing, clip pulses) | on | on | off | off | off |
| Pattern editor scrolling | line by line | line by line | line by line | **page** | page |
| Mouse reporting | any motion | any motion | drag only | drag only | drag only |
| Pointer-shape updates | on | on | on | off | off |
| Colour depth | as detected | as detected | as detected | 256 | 256 |
| Dials | images, always | images | images | images | images |

The table is a starting point; phase 0 numbers decide the cuts. The budget may
keep more than the row says, never less.

## 3. The steps

### 3.1 Compress first (every class, no loss)

The largest cost is the chart pictures: raw RGB, base64, about 26 kB for an 80x80
picture, every frame, and each protocol sends it uncompressed. Before dropping
anything:

- **kitty:** `o=z` (zlib) on the payload. A line chart on a flat background
  should compress many times over; a smooth heatmap much less. **No new
  dependency:** zlib is not a declared dependency (cairo brings it in
  incidentally), so this needs a small in-tree deflate encoder (LZ77 with fixed
  Huffman codes, a few hundred lines, with a PNG writer that already exists for
  iTerm2 in `encodePng()`). Measure the ratio on the real charts before building it.
- **PNG (`f=100`)** is the other route and also allows 8-bit greyscale or palette
  images (1 byte per pixel or less) where kitty's raw formats are 3 or 4. Whether
  every kitty-capable terminal decodes greyscale and palette PNGs must be checked
  per terminal; raw RGB with `o=z` is the safe baseline.
- iTerm2 already sends PNG (uncompressed "stored" blocks): the same encoder
  compresses it.

### 3.2 Greyscale charts

The spectrum, the heatmap, the sparklines and the waveform get a `ChartLook`
(`COLOUR`, `GREY`) that `Charts` implementations honour; the pixel renderers draw the
same picture in grey levels.

- **Bytes saved depend on the protocol.** Raw kitty RGB has no 1-byte format, so
  greyscale only pays with `o=z` (R=G=B compresses far better) or PNG grey. Sixel
  benefits most: 16-32 grey levels instead of 240 colour registers means far fewer
  colour passes per band.
- **The heatmap needs a decision.** It is drawn from a brightness and a saturation
  grid (`setGrid(brightness, saturation)`). If saturation carries information,
  plain grey drops it. Alternative: keep colour but quantise it to a few levels,
  which compresses nearly as well. Decide after looking at the DirAC data, with
  you.
- The glyph chart versions stay as they are: the fallback for a display with no
  pictures, and a last resort at `HZ_1` only.

### 3.3 Lower-resolution heatmap

`Image` already supports being stretched over cells by the terminal
(`cols`/`rows`/`pad`), so the picture can be drawn at 1/2 or 1/4 resolution. Today
the heatmap's axis labels are drawn **into** the image, which would blur them. Two
ways to separate them:

- **Margin cells (recommended):** the labels move out of the picture into cells
  around it. Works on kitty, iTerm2 and Sixel, and for the window too. Needs the
  scope column's layout and `HeatmapChart`'s contract changed.
- **Text over the picture:** a z-index below the text. kitty only (neither Sixel
  nor iTerm2 can layer text over an image), so it would work for one protocol.

Only the heatmap is lowered; the spectrum line stays sharp.

### 3.4 Scope and meter rates

Scopes and meters are updated at the class's rate rather than on every frame, by
the app: the visualization results already arrive as events and the UI redraws
nothing for an unchanged result, so this is a gate on applying them. Scopes off
means the plane is hidden and not updated (they already skip updating off-screen).

### 3.5 Animations off

`InteractiveUI::setReducedMotion()` already makes states change at once. At
`HZ_15` and below it is switched on by the profile: smooth scrolling, easing of
cursors and views, the clip slots' pulsing (a steady colour instead) and the
flashing of queued slots (a steady marker).

### 3.6 Page scrolling in the pattern editor

At `HZ_5` and below (or RTT over 250 ms) the editor stops following the cursor and
playhead a line at a time: when the cursor or a playing column's line would leave
the visible rows, the view **jumps one page** (visible rows minus the scroll
margins) so the whole screen repaints once per page instead of once per row.

- `PatternEditor` gets a `ScrollMode { LINE, PAGE }`; in Live View, each column's
  line (`keepTrackLinesVisible()`) uses the same rule, so a playing track's column
  turns its page when its playhead reaches the margin.
- Applies to both views; the arrangement grid pages sideways by bars the same way.
- Text-only frames are not free either: a scrolled screen changes every cell. I
  have not measured how many bytes that is; phase 0 does.

### 3.7 Upstream traffic and colour depth (backend-level)

- **Mouse:** `?1003` (any motion) → `?1002` (drag only) at `HZ_15` and below; every
  pointer move is bytes sent up a possibly slow line. Hover effects stop, which is
  acceptable there. Pointer-shape updates (OSC 22) stop at `HZ_5`.
- **Key release events:** the kitty keyboard flags include event types; turning
  them off halves the upstream key traffic. Checked first against `KeyReader` and
  the note entry that uses releases.
- **Colour depth:** `EncoderProfile::colors` TRUECOLOR → 256 at `HZ_5` and below.
  It saves a few bytes per attribute change, small next to picture data, which is
  why it is last. A change forces one full redraw, so it follows the hysteresis.

### 3.8 Sixel: send only what changed

Today `ImageEncoder::sixel()` sends the **bounding box** of an image's visible
rectangles, and again whenever anything about it changes (a popup over a corner,
a new version). Fix:

1. **One sixel per visible rectangle**, not one for their union, so a popup over a
   chart no longer resends the covered pixels as transparency.
2. **Tile diff:** when only the picture's version changed, compare it with the
   previous pixels (kept with the image already shown) in cell-aligned tiles, and
   send only the changed tiles, merged into rectangles per row. Cell alignment is
   forced: a sixel starts at a cell origin. The last band of a tile is partial
   when the cell height is not a multiple of 6.
3. A changed tile is dithered with the **same screen-anchored matrix** as now
   (`Dither::origin_*`), so a tile drawn alone matches the one next to it.

Tests (`ImageEncoderTests`): a popup over a chart sends only the visible
rectangles; a one-row change sends only the tiles it touches; an unchanged picture
sends nothing; a Sixel image removed sends nothing and the text overwrites it. The
same diff idea could later apply to kitty with its frame-edit command, where the
terminal supports it (`Capabilities::kitty_graphics_animation`); not planned now.

### 3.9 Dials

No change, by decision. A check in `PerformanceProfile`'s tests asserts no class
selects a glyph dial.

## 4. GTK bandwidth

A class for the window (section 1) comes from the frame clock and needs no
bandwidth. A bandwidth estimate is possible only in part:

- **Local window:** no meaningful bandwidth; report 0 (unknown), not a made-up
  number. The class is still derived from the refresh rate and the achieved frame
  interval.
- **Remote display** (X forwarding, VNC, Broadway; detected from the display name,
  `SSH_CONNECTION`, or the GDK backend): the pixels cross a link we cannot see.
  What we can observe is how long each frame takes to complete
  (`gdk_frame_timings_get_complete()`/presentation time) against the bytes of
  texture uploads we made for it (the sizes are known: `imageTexture()`). Feeding
  those into the same estimator as the terminal's (`FramePacer`: frame sent =
  snapshot, ack = frame complete) gives an **effective throughput of our
  pictures**, a proxy, not the link's bandwidth.
- **Limits:** frame timings are reliable only on some backends; GL/Vulkan renderers
  hide the upload; compositors batch. So the number is marked estimated and shown
  only when the display is remote and the timings are present.
- **Testing:** there is no remote GTK in the container. A relay in front of Xvfb's
  socket, shaped like the pty harness, would emulate one (bandwidth, latency); it
  is a task of its own and can come last.

## 5. Phases

0. **Measure.** Run the real app with playback and scopes through the shaped pty;
   log bytes per second by kind and by picture key once a second
   (`SYNTH_LINK_STATS=<file>`, like the existing UI stats). Gives the reference frame
   and every feature's cost. *Output: the thresholds and the cuts in section 2.*
1. **API and classes.** `LinkStatus`, `Backend::linkStatus()/setLinkHandler()`,
   terminal classification with hysteresis and the budget, the status bar, `--link-class`
   and `set-link-class`, `TestBackend::setLinkStatus()`. Harness assertions: the class
   at 5 bandwidths, no flapping on the 524/589 noise, a pinned class stays.
2. **Sixel fix** (3.8). Independent of the rest; tests as listed.
3. **Compression and greyscale** (3.1, 3.2). Harness: bytes per frame per protocol
   before and after on the real charts.
4. **App profile:** scope/meter rates and off, animations off, page scrolling
   (3.4-3.6). UI tests per class with `TestBackend`; a harness run at 50 kB/s
   asserts the editor stays usable (key latency, fps).
5. **Backend adaptations:** mouse, key releases, colour depth (3.7).
6. **Heatmap resolution** with margin labels (3.3).
7. **GTK class** from the frame clock, then the **experimental bandwidth** (section 4).

## 6. Decisions I need from you

1. **Heatmap in grey:** grey (drops saturation if it carries data) or a few
   quantised colour levels?
2. **Deflate:** write a small in-tree encoder (my recommendation, no dependency)
   or use zlib that cairo already links?
3. **Heatmap labels:** margin cells (all protocols) or text over the picture (kitty
   only)?
4. **Mouse:** is losing hover feedback at 15 Hz and below acceptable?
5. **Window classes:** is `HZ_120` worth distinguishing, or should the window's top
   class be 60?

## 7. Risks

- **Flapping.** Mitigated by hysteresis, the budget and trials, but the estimate
  is noisy and content-dependent; the first version should log every class change.
- **Estimate while quiet.** A static UI sends nothing, so B goes stale; the class
  should hold, not fall, while nothing is sent.
- **Protocol support.** Greyscale/palette PNG and `o=z` are standard but not
  tested on every terminal; the fallback is the raw RGB that works today.
- **Page scrolling is a behaviour change** users will notice; it is only on in
  the slow classes and can be pinned off.
