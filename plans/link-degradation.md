# Degrading gracefully on a slow link

Status: plan, third revision. Phase 0 (measuring) is done and its tools are in the tree; the rest
is awaiting approval.
Builds on the terminal frame pacer (`plans/frame-pacing-terminal.md`), which
measures the link but changes nothing about what is drawn.

## Decisions made

- **Colours stay, on every terminal that has any.** 8- and 16-colour terminals keep
  colour: it is how tracks are told apart. There is **no automatic grey**: switching part
  of the screen to grey would mix grey and colour, and repainting everything to avoid that is
  not worth it. Grey is an option on request only and may never be built (section 2).
- **A slow link lowers picture quality, not screen colour**: pictures are rectangles, so a
  reduced palette in a chart mixes with colour text without a visible seam (3.2).
- **Dials are never glyphs.** They are images in every class, **15 positions at 3 dB**, **the letter
  stays in the picture**, four looks, shared by a custom key (3.5).
- **Compression is a small in-tree deflate encoder**, no new dependency (3.1).
- **No label changes and no reduced-resolution pictures**, the heatmap included.
- **Sixel gets a real palette and stops resending its bounding box** (3.3).
- **Hover feedback may be lost at 15 Hz and below.** Hover effects are coming and will cost
  more, so they are a managed cost (3.6).
- **No `HZ_120` class.** The status string is the backend's own (`LinkStatus::description`):
  bandwidth for a terminal, the real vsync rate and achieved fps for a window. No bandwidth
  estimate for the window.
- **The API may change.**

## 1. Link status

```cpp
// cellui/LinkStatus.h
enum class LinkClass { FULL, HZ_30, HZ_15, HZ_5, HZ_1 };

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

`TestBackend` returns a status the test sets (`setLinkStatus()`), so the app can be tested
per class without a pty.

### What a class is

A class is a **profile**: a set of choices (which scopes run and how often, animations,
scrolling mode, picture quality, hover, mouse reporting). Each profile has a **demand**: the
bytes per second it needs to look as intended, measured in phase 0. The link's class is
the **largest profile whose demand fits in 70% of the estimated bandwidth**. So the thresholds
are not guessed: they are the measured demands of the profiles (phase 0 below).

- `FULL` is the profile that sends everything the app draws, at whatever rate the pacer
  allows up to 60 Hz. Nothing is cut between 120, 60 and 60-ish, so those are one class.
- `HZ_30`, `HZ_15`, `HZ_5`, `HZ_1` are smaller profiles, named for the update rate they
  sustain for the content that is left.
- A window's class comes from its frame clock: the achieved frame interval against the
  refresh interval (`gdk_frame_clock_get_refresh_info()`, frame timings), so a slow remote
  display (X forwarding, VNC, Broadway) lands in a low class with no bandwidth figure.
- An RTT over 250 ms also selects page scrolling (3.7), whatever the class.

### The description

- **Terminal:** the pacer's estimate and RTT, `"480 kB/s, RTT 38 ms"`; for a terminal that
  answers no cursor report, `"no cursor reports, 4 Hz"`.
- **Window:** the real vsync rate of the monitor the window is on (`gdk_monitor_get_refresh_rate()`,
  mHz) and the frame rate achieved over the last second: `"60 Hz vsync, 58 fps"`; a fractional
  rate shows as such (`59.94 Hz`).
- The status bar shows the description whenever the class is below `FULL` or pinned
  (`Link: 15 Hz, 480 kB/s, RTT 38 ms`), and nothing on a good link; showing a window's vsync
  rate always is a one-line option.

### Stability

The estimate is noisy (524 and 589 kB/s for a true 520 in two runs):

- **Down fast:** two seconds below a threshold moves one class down.
- **Up slowly:** fifteen seconds above 1.5x the next demand, and only after a **trial**. A
  feature must not come back just to overload the link again.
- No change while the pacer is in a stall or in the first seconds after start-up.
- A static UI sends nothing, so the estimate goes stale: the class **holds**, it does not fall.

### Probing upward

With scopes off there is little to measure. A recovery enables the next feature for a few
seconds as a trial; the pacer's own queueing-delay rule judges it. A failed trial waits
longer before the next.

### Pinning

`--link-class full|30|15|5|1|auto` and the command `set-link-class` (with a line in
`ui/CommandDescriptions.cpp`); `auto` returns to measuring.

## 2. Colours

- **Truecolour and 256-colour terminals:** unchanged.
- **8- and 16-colour terminals keep colour, and get a palette made for them.** Today every
  colour is mapped to the nearest of eight by RGB (`nearest8()`), which ignores what the
  terminal's eight colours actually look like. Instead the theme gets an `ansi16` variant
  that names colours by **role** and sends palette indices:
  - text and background are the terminal's own defaults (`SGR 39`/`49`), so the user's
    terminal theme guarantees contrast;
  - accents are the terminal's six hues, bright variants for emphasis;
  - **track colours** come from a fixed table of the six hues in normal and bright (twelve),
    assigned so that neighbouring tracks differ; with only eight colours the table drops to
    the six hues and bright is the emphasis.
  A test checks that every semantic pair that must differ does (cursor, selection, bar, beat,
  playhead, focus), by index.
- **Track colours by ordinal, with a fallback table.** A track's colour already comes from its
  ordinal (`TrackPalette`: the modern table gives the first eight tracks hues picked to be far
  apart, later ones step by the golden ratio). A third model, `TrackPalette::Model::ANSI`, is a
  fixed table from ordinal to a palette colour, **ordered so that the first N tracks are as
  different as the palette allows for every N**: with sixteen colours the six hues in an order
  that alternates warm and cool (yellow, blue, red, cyan, magenta, green), then their bright
  versions; with eight, the six hues and then the same six again drawn bold. Its colours are the
  standard palette's own RGB values, so the encoder's nearest-colour step returns exactly the
  intended index and the encoder does not change. This needs a sixteen-colour depth in the encoder
  (`ColorDepth::C16`, `SGR 90-97/100-107`), which today folds into eight. It is chosen by depth,
  so truecolour keeps the modern table.
- **No colour at all (`MONO`)** is still a mode, for very old terminals: highlights use
  **reverse video** (`SGR 7`), which the cell encoder does not emit today (with no colour a
  highlight is invisible now), dim text uses `SGR 2`, emphasis bold.
- **Grey on request** (`--colours grey`): luminance conversion in the encoders (and 4 levels
  on a 16-colour terminal). Lowest priority; if nothing needs it, it is not built.

## 3. The steps

### 3.1 Compress, in tree (every class, no loss)

The biggest cost is chart pictures: raw RGB, base64, every frame, in every protocol. Before
cutting anything:

- A small **zlib-format deflate encoder** in `cellui/term/Deflate.{h,cpp}`: LZ77 with a hash
  chain and fixed Huffman codes (dynamic Huffman only if the measured ratio needs it), with
  Adler-32. Round-trip tested against a small inflate written for the tests.
- **kitty:** `o=z` on the payload. PNG (`f=100`) uses the same encoder for its IDAT stream,
  with a CRC-32 writer next to the PNG writer that exists (`encodePng()`, for iTerm2, which is
  stored uncompressed today).
- The ratio is measured on the real charts first. A line chart on a flat background should
  shrink many times; a smooth heatmap much less.

### 3.2 Picture quality on a slow link

A `PictureQuality` knob (`FULL`, `REDUCED`, `LOW`), applied by the image encoder, is what a slow
link turns instead of screen colour:

| | FULL | REDUCED (HZ_15) | LOW (HZ_5 and below) |
|---|---|---|---|
| kitty, iTerm2 | RGB, deflated | palette PNG, up to 64 colours, 8-bit indices deflated | up to 16 colours, 4-bit indices |
| Sixel | exact or up to 256 registers | up to 64 | up to 16 |

- A palette PNG (colour type 3) is standard; whether every kitty-protocol terminal decodes it is
  not known. The **error reply (`q=1`) is the test**: on one, the encoder falls back to
  quantised raw RGB with `o=z` and remembers it for that terminal.
- Charts have few colours (a flat background, a line, its anti-aliasing), so a 64-colour
  palette is close to lossless for them; the heatmap, a single hue at two variables, lies on a
  two-parameter surface, so a palette made of a grid over those two (about 12 x 8) covers it.
  Palettes are made from the picture's colour histogram (median cut when over the limit), not
  fixed.
- Greyscale pictures are not needed for this. (Kitty's PNG does take true greyscale, colour type
  0, 8 or 4 bits per pixel; it would come with the optional grey mode.)

### 3.3 Sixel: palette and damage

**The palette today.** `encodeSixel()` maps every pixel to one of **240 fixed registers**: the
xterm colour cube (6 levels per channel: 0, 95, 135, 175, 215, 255) plus the 24-step grey ramp,
then dithers with an 8x8 ordered (Bayer) matrix. The terminal's own register count (asked at
start-up, `Capabilities::sixel_registers`, up to 1024) is read and never used. Why it looks bad:

- six levels per channel cannot hold the theme's colours exactly, so a **flat background that
  is not on the cube is dithered into a pattern**;
- the heatmap's smooth single-hue gradient **bands** (a handful of levels along its hue);
- anti-aliased chart lines pick up dither noise;
- the palette ignores the picture, so a picture with five colours still pays the 240-register
  layout.

**The fix.** A palette per picture, from its colours:

1. Count the distinct colours. If they fit in the registers, use them **exactly, with no
   dithering**. Measured on the real pictures (phase 0): the small meters have 3 to 88 colours, the
   spectrum **335 to 451** (anti-aliasing) and the heatmap **about 1100**. So most fit when the
   terminal's register count is used (256 is the common figure, up to 1024 or more where it says
   so), and the heatmap is the one that needs quantising.
2. Otherwise median cut to N registers (N from `PictureQuality` and the terminal's count) and
   dither lightly; for the heatmap, the two-variable grid of 3.2. What a **register** is: a sixel
   image paints with colours it has first defined in numbered slots of the terminal's colour table
   (`#12;2;r;g;b` defines slot 12, `#12` then paints with it), and a terminal has a limited number of
   slots for one image, its register count. The encoder asks for 1024 at start-up, the terminal says
   what it will give, and the answer is stored but unused: the encoder always spends 240.
3. Fewer colours is also fewer colour passes per band, which is most of a sixel's size.
4. Whether colour registers persist between images (`?1070`, private versus shared registers)
   decides if the palette can be reused across frames; to be checked on the terminals that can
   be tried. If shared, a stable palette per widget saves resending it every frame.

**Damage.** `ImageEncoder::sixel()` also sends the bounding box of an image's visible
rectangles, again whenever anything about it changes. Fix:

1. **One sixel per visible rectangle**, not their union, so a popup over a chart no longer
   resends the covered pixels as transparency.
2. **Tile diff:** when only the picture's version changed, compare with the previous pixels (kept
   with the image already shown) in cell-aligned tiles and send only the changed tiles, merged
   per row. A sixel starts at a cell origin, hence the alignment; a tile's last band is partial
   when the cell height is not a multiple of 6.
3. A changed tile uses the same palette as its neighbours, and the same screen-anchored dither
   where dithering applies (`Dither::origin_*`).

Tests (`ImageEncoderTests`): an image of few colours round-trips exactly through the palette; a
flat colour off the old cube is flat; a popup over a chart sends only the visible rectangles; a
one-row change sends only the tiles it touches; an unchanged picture sends nothing; a removed
image sends nothing and the text overwrites it.

### 3.4 One upload per picture, many placements, by a custom key

Today each placed picture has its own key from a counter, and a key's first appearance is an
upload, so two dials with identical pixels are two uploads, and an explicit `id` given to
`drawImageAt()` only keeps that one placement's key stable. The change:

```cpp
// Plane
// A picture whose pixels depend only on `key` (the caller promises it): drawn once for the
// root, shared by every plane that asks for the same key, and only rendered when no one has.
virtual void drawSharedImageAt(int row, int col, uint64_t key, int width, int height,
                               const std::function<std::vector<uint32_t>()> & render);
```

- The plane tree keeps a **registry of shared pictures** by key. `render` runs only when the key
  is not in the registry, so a dial on screen costs no rasterisation after its first drawing, and
  nothing is hashed per frame. A change of theme, display scale or cell size bumps a generation
  that empties the registry.
- `ImagePlacement` gains `shared_key`. The kitty encoder uploads **once per shared key** and
  places it with `a=p` wherever it is shown (a placement id per position). The GTK window
  already keys its textures by image, so one texture serves every placement.
- A shared picture is **not deleted** when its last placement goes (`a=d,d=i` removes the placement
  and keeps the data); the shared set is bounded by bytes (a few MB), evicting the least recently
  placed with `a=d,d=I`. One-use pictures (charts) keep today's behaviour.
- Pictures are uploaded **lazily**, on first use, never all at start: on a slow link an up-front
  burst of every dial would itself be the problem.
- Sixel and iTerm2 keep no stored images, so sharing saves rasterisation but not bytes there.

### 3.5 Dials

Dials stay images in every class. To make them cacheable:

- **15 positions**: the dial's arc is -42 dB (drawn as off) to 0 dB, at **3 dB** steps, which is the
  Launchpad's own grid (below). The pointer is drawn at the nearest. What the code has: the Launchpad
  steps its fader rows 6 dB apart (off, then -36 to 0 dB) and a repeated press on a row moves
  **halfway to the next** (3 dB), and a second repeat is the next row, so its reachable values are off,
  -36, -33, ..., -3, 0: 14 values. (The code comment counts 8 rows x 2 = 16; a micro-tap on the bottom
  row also lands at -68 dB, half way between off and -36, which looks unintended and would show as
  off.) Dials have a 3 dB drag step per row and a 1 dB wheel step; I find no 2 dB step in the code. 16
  positions would need a 2.8 dB step, which is fractional; 15 at 3 dB needs no change to the Launchpad
  and adds only the dial's -42 and -39 dB below the Launchpad's lowest, so it is what I plan unless you
  say otherwise.
- **Four looks** per position: normal, pressed, hover, selected. `hover` is new; all four are
  implemented now so they can be tested.
- **The letter is in the picture**, so A, B and Main are three sets: 15 x 4 x 3 = **180 pictures**,
  each keyed by (letter, position, look, scale), none rendered until used. They are small and
  flat, so they compress to a few hundred bytes each and are well under 1 MB in the terminal.
- Dragging with Shift (fine) still sets the exact value; the pointer moves when it crosses a
  position. The typed value shows as text while editing, as now.
- Tests: all four looks at every position render and are distinct where they should differ;
  equal key gives equal pixels; each is uploaded once however often shown (kitty); the gallery
  shows the grid.

### 3.6 Hover is a cost

Hover effects (coming) are redraws triggered by the pointer, and so are bandwidth:

- **Mouse reporting** `?1003` (any motion) falls back to `?1002` (drag only) at `HZ_15` and below,
  and hover effects stop there (accepted).
- **Pointer-shape updates** (OSC 22) stop at `HZ_5`.
- A hover look reuses shared dial pictures (3.4), so hovering a dial costs a placement, not an upload.
- A new hover effect declares its cost and whether it needs a new picture; one that does is a
  candidate to switch off sooner.

### 3.7 App behaviour by class

Content decisions live in the app (`src/ui/PerformanceProfile.h`: `profileFor(LinkClass)`, applied
by `InteractiveUI` when the link handler fires).

| | FULL | HZ_30 | HZ_15 | HZ_5 | HZ_1 |
|---|---|---|---|---|---|
| Picture quality | full | full | **reduced** | low | low |
| Scopes (spectrum, heatmap) | every frame | 30 Hz | 15 Hz | **off** | off |
| Channel meter | every frame | every frame | 15 Hz | 5 Hz, peak only | off |
| Level meters (clip grid, pattern editor) | every frame | every frame | 15 Hz | 5 Hz | cell meter, last resort |
| Animations (smooth scroll, easing, pulses) | on | on | **off** | off | off |
| Pattern editor scrolling | line by line | line by line | line by line | **page** | page |
| Hover effects, any-motion mouse | on | on | **off** | off | off |
| Pointer-shape updates | on | on | on | off | off |
| Dials | images | images | images | images | images |

The table is the starting point; phase 0's demands set the cuts.

- **Animations** use `InteractiveUI::setReducedMotion()`, which exists: smooth scrolling, easing,
  the clip slots' pulsing (a steady colour instead) and flashing (a steady marker).
- **Page scrolling** (`PatternEditor::ScrollMode { LINE, PAGE }`): when the cursor, or a playing
  column's line, would leave the visible rows, the view jumps one page (visible rows minus the
  scroll margins), so the screen repaints once per page instead of per row. In Live View each
  column's line (`keepTrackLinesVisible()`) uses the same rule; the arrangement grid pages sideways
  by bars.
- **Key-release events** from the kitty keyboard protocol stop at `HZ_5` if `KeyReader` and note
  entry can do without them (checked first).
- The glyph (cell) charts stay the fallback for a display with no pictures, and a last resort at
  `HZ_1` only.

## 4. Phase 0: what is measured, and what it gives

Phase 0 decides the thresholds, so it is specified here.

**What is measured** (a log line a second, `SYNTH_LINK_STATS=<file>`, like the existing UI stats):
frames, bytes by kind (upload, placement, other), and uploads and bytes **by picture size**, so each
widget's cost can be told apart, in these scenarios through the shaped pty, 120x40 cells of 8x16 px,
demo3.xml, kitty: Live View idle; Live View playing; Arrangement view playing; Arrangement view with
the pattern editor scrolling (Down at 30 a second); the same with page scrolling once built; mouse
movement over the screen (hover cost); a dial dragged.

**Measured** (`tools/pacing/measure_link.py`, `analyze_pictures.py`; the real app through the emulated
terminal on an unshaped link, `--null-audio`, kitty, 120x40 cells of 8x16 px, `demo3.xml`; bytes as the
terminal receives them, pictures raw RGB in base64):

| Scenario | kB/s | Pictures | Text etc | Frames/s | Uploads/s |
|---|---|---|---|---|---|
| Live View, idle | 0 | 0 | 0 | 0 | 0 |
| Live View, playing | **5540** | 5500 | 14 | 60 | 443 |
| Arrangement view, playing | **6390** | 6290 | 83 | 59 | 292 |
| Arrangement, pattern editor, Down 30/s | 249 | 0 | 249 | 26 | 0 |
| Live View, mouse moving 30/s, stopped | 0 | 0 | 0 | 0 | 0 |
| Live View, playing + mouse moving | 5500 | 5460 | 14 | 60 | 433 |

By picture, playing: **Live View** 232x80 at 62/s (4640 kB/s), 8x96 at 249/s (765), 8x16 at 132/s (68);
**Arrangement** 248x80 at 46/s (3680), 328x80 at 16/s (1640), 72x64 at 46/s (855), 8x16 at 183/s (94).
Mouse movement costs nothing today because there are no hover effects; this is the baseline for them.
At start the 16 dials (40x48 px) go up once each, 124 kB in all, and never again.

What the pictures are made of (`analyze_pictures.py`, the stand-in for the in-tree encoder being zlib at
level 1, so the real encoder will do somewhat worse):

| Picture | Colours | Raw | Deflated | Pixels changed per frame | 8x16 tiles changed |
|---|---|---|---|---|---|
| 232x80 (Live View's chart) | 451 | 55.7 kB | **3.1 kB (18x)** | 6% | 20% |
| 248x80 (Arrangement's chart) | 335 | 59.5 kB | **2.9 kB (20x)** | 5% | 13% |
| 328x80 (heatmap) | 1088 | 78.7 kB | 5.4 kB (15x) | 8% | 36% |
| 72x64 (meter) | 88 | 13.8 kB | 0.8 kB (17x) | 2% | 33% |
| 8x96 (level meters) | 57 | 2.3 kB | 0.32 kB (7x) | 1% | 17% |

- **Compression alone** takes Live View's playing traffic from 5.5 MB/s to about 0.37 MB/s and
  Arrangement's from 6.4 to about 0.36 MB/s (deflated sizes times the number of uploads, plus base64's
  4/3): a fifteen- to eighteen-fold cut with no change to what is drawn.
- **Sending only what changed does not pay after compression**, for kitty and iTerm2. Only 5-8% of
  pixels change, but they are spread across the whole width (a chart's curve moves in every column), so
  the changed tiles are 13-36% of the tiles and the bounding box of the change is almost the whole
  picture. Sent as separate deflated tiles they cost **more** than the whole picture deflated (5.0 kB
  against 3.1 kB for the chart at one tile per cell; larger tiles do better, 3.5 kB at 4 cells, never
  better than the whole), because every tile pays a header and starts its compression afresh. Only
  the level meters gain (143 B against 319 B), and they are a few hundred bytes. So: compress whole
  pictures; do not tile for kitty or iTerm2.
- **For Sixel it does pay**: the format has no deflate, a changed picture costs a colour pass per
  band whatever changed, and 80% of the tiles are unchanged. So the tile diff of 3.3 stays in the plan
  for Sixel, where it is worth roughly four to five times for these charts.
- Kitty has a command to edit part of an existing image in place (the animation frames, `a=f`, in kitty
  from 0.20 only, `Capabilities::kitty_graphics_animation`); with whole pictures at 3 kB it saves nothing
  that matters and works on one terminal, so it is left out.

**What it gives:** each feature's demand (bytes a second) with deflate (3.1) in place (the estimate above
is zlib; the in-tree encoder's own ratio replaces it), each
profile's demand as the sum of its features, and from them the class thresholds: the class's
bandwidth threshold is its profile's demand divided by 0.7. The thresholds are not set until the
deflate ratio on real charts is known.

## 5. Phases

0. **Measure** (section 4). **Done**: the tools (`measure_link.py`, `analyze_pictures.py`), the
   `SYNTH_LINK_STATS=<file>` log for a real link (a line a second: frames, bytes by kind, bandwidth, RTT,
   uploads by picture size), and the results above. Still to do on real hardware: run it over a real SSH or
   serial link, and attribute the pictures by widget rather than size.
1. **API and classes.** `LinkStatus`, `Backend::linkStatus()/setLinkHandler()`, classification by
   profile demand with hysteresis and trials, the description strings (terminal bandwidth/RTT,
   window vsync and fps), the status bar, `--link-class` and `set-link-class`,
   `TestBackend::setLinkStatus()`. Harness checks: the class at five bandwidths, no flapping on the
   524/589 noise, a pinned class stays.
2. **Deflate, PNG and picture quality** (3.1, 3.2): the encoder and its round-trip tests; bytes per
   frame per protocol before and after on the real charts; the kitty fallback on an error reply
   tested with the harness's emulated terminal answering one.
3. **Sixel** (3.3): palette and damage.
4. **Shared pictures and dials** (3.4, 3.5): the registry, `drawSharedImageAt()`, the byte cap, the
   16-position, four-look dial set.
5. **App profile** (3.6, 3.7): scope/meter rates and off, animations, page scrolling, hover, mouse
   reporting. UI tests per class with `TestBackend`; a harness run at 50 kB/s asserts the editor
   stays usable (key latency, fps).
6. **Colours** (section 2): the `ansi16` theme and track-colour table, `MONO` with reverse video;
   grey only if asked.
7. **Window class** from the frame clock and vsync (section 1).

## 6. Decisions I need

1. **Dial positions:** 15 at 3 dB (the Launchpad's grid, plus the dial's -42 and -39), as in 3.5, or do you
   want exactly 16? 16 needs a 2.8 dB step, or a change of the Launchpad's row spacing.
2. **Classes by demand:** is "the largest profile that fits in 70% of the bandwidth" the right rule? Phase 0
   now gives the inputs; thresholds follow once the in-tree encoder's ratio is known.

## 7. Risks

- **Flapping.** Hysteresis and trials mitigate it, but the estimate is noisy and content-dependent;
  the first version logs every class change.
- **Protocol support.** Palette PNG, `o=z` and sixel register behaviour are standard but untested
  on every terminal; the fallback is quantised raw RGB, then today's raw RGB. No real sixel or
  kitty terminal is available here, only the emulated one.
- **Cache memory in the terminal** is outside our control; the byte cap is conservative and
  eviction explicit.
- **Page scrolling** is a behaviour change; it is only on in the slow classes and can be pinned off.
