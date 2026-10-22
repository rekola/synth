# Cell UI, phase 2: the window

The graphical backend of Cell UI (`plans/cell-ui.md`). It starts once phase 1
(`plans/cell-ui-terminal.md`) has separated the UI: the app then talks only
to `cellui`, and this phase adds a backend without touching the widgets.
Linux (X11/Wayland, whenever a display server is running) and Windows; on
Linux the terminal stays selectable (`--ui window|terminal`, the default
chosen by whether a display is available), and Windows has only the window.

Decided: on Linux the window backend is a **GTK** backend for Cell UI; on
Windows it is written directly against **Win32**, drawing with Direct2D and
DirectWrite (below). No cross-platform toolkit sits under either.

## Host (decided: GTK on Linux, Win32 on Windows)

Cell UI doesn't have to sit on SDL3. Its window backend can also be hosted by
an existing toolkit. The toolkit then only supplies windows, input, text,
menus and dialogs, while the cells, styles, spans and icons are still drawn
by our code into one surface. That is different from a Qt or wxWidgets UI:
no widget is rewritten, the app still only sees Cell UI, and the terminal
backend is unaffected.

| Host | Gives us | Costs us |
|---|---|---|
| SDL3 (the plan above) | Small, simple, game-style rendering freedom (SDL_GPU) | Native menus, IME details, font fallback, accessibility adapters, i18n tooling are all ours (the "lack" table in `plans/cell-ui.md`) |
| Qt 6 | Nearly the whole "lack" table: accessibility (UIA/AT-SPI through `QAccessibleInterface`, which we implement for the grid), IME, HarfBuzz shaping and font fallback, HiDPI, translation tooling, native menus on Windows and a native-looking menu bar on Linux (with KDE's global menu), modal dialogs, native file dialogs. Qt Quick's scene graph renders through Vulkan/D3D/Metal with custom shaders, so shadows, blur, layers and 3D transforms stay possible; Qt Quick 3D XR covers a headset later | Large runtime (tens of MB in the installer), Qt's build tooling, Qt's event loop on the main thread, LGPL obligations for an MIT project (dynamic linking, relinkable), and some modules (Qt Quick 3D among them) are GPL or commercial only, which rules them out unless the licence changes |
| wxWidgets | Truly native menus and dialogs on every platform, GTK ones on Linux; lighter than Qt; a licence that allows static linking | A GPU surface (`wxGLCanvas`) inside native widgets is the weak spot, especially on Wayland; accessibility outside Windows is thin, and IME into a custom-drawn surface is ours again; effects need our own OpenGL anyway |
| Dear ImGui | Fast tool and debug panels | Nothing on the "lack" table: it needs a platform layer (SDL3 itself), draws its own menus, and has no accessibility or real IME. Useful at most for developer panels inside the window backend, not as the host |

Qt 6 was recommended as one host for both platforms, for the long tail of
accessibility, IME, fonts, translation and platform menus. With GTK on Linux
it would serve Windows alone, and Windows is the platform whose own APIs
already cover that tail:

| Item | Qt on Windows | Win32 directly |
|---|---|---|
| Text shaping, font fallback, emoji | HarfBuzz in Qt | DirectWrite |
| HiDPI | handled | per-monitor v2 in the manifest, `WM_DPICHANGED` |
| IME | handled | IMM32 messages; needed only where text is typed (the minibuffer, name editors) |
| File dialogs, message boxes | native wrappers | the same native dialogs (`IFileOpenDialog`) |
| Menus | wraps `HMENU` | `HMENU` |
| Accessibility | UIA bridge, the grid still ours (`QAccessibleInterface`) | a UIA provider of our own, the grid included |
| Size, licence, build | tens of MB of DLLs, LGPL relinking, Qt's tooling | nothing beyond Windows, the existing MinGW build |
| Event loop | Qt's on the main thread | ours: `MsgWaitForMultipleObjectsEx` waits on messages and `Notifier` event handles, as `WaitSet` does |

Accessibility is the one place Qt saves real work, and less than it seems:
nearly all of the UI is custom-drawn cells that we describe either way; Qt
would save the window-level plumbing (the root provider, events, focus).
That is the accepted cost. GTK on Windows (one host for both) was also
considered: it has no UI Automation backend on Windows and brings a large,
foreign-looking runtime, so it loses on what Windows users notice.

Revisit only if a spike shows Direct2D/DirectWrite drawing the cells worse,
or if accessibility on Windows is needed from the first release.

## Windows: Win32

- **Target:** Windows 10 1903 or later and Windows 11, x86-64 (the MSYS2
  UCRT64 build). 1903 brings the manifest's UTF-8 code page; per-monitor
  DPI v2 needs 1703. The dark title bar (`DWMWA_USE_IMMERSIVE_DARK_MODE`) is
  used where the OS has it and silently absent where not. ARM64 later, from
  MSYS2's Clang toolchain, if asked for.
- **Window and input:** `CreateWindowExW` and a window procedure, messages
  translated into `cellui::Input`. Alt arrives as `WM_SYSKEYDOWN`; F10 and
  a bare Alt are taken from the system menu so Meta chords reach the
  keymap; AltGr (seen as Ctrl+Alt) types its character.
- **Drawing:** Direct2D into a DXGI flip-model swap chain on Direct3D 11,
  waitable for vsync pacing; DirectWrite for text and font fallback, WIC
  for images. An idle UI presents nothing.
- **HDR** (colour model and renderer choice: `plans/cell-ui-hdr.md`). The
  Direct2D renderer above is the `basic` renderer: it reads `Color`s through
  `toSrgb8()` (a wide colour is clipped), 8-bit, and knows nothing of HDR.
  The `effects` renderer, on Direct3D, is chosen instead of it at start by a
  probe, never switched to or from at run time and with no fallback inside a
  frame. Windows has no host question: the Win32 window owns its swap chain,
  and Direct3D renders straight into it (no OpenGL interop, no ANGLE).
  - *The probe* (before the window is shown): a hardware Direct3D device
    (not WARP; feature level to be fixed with the device layer spike,
    Direct3D 11 or 12) with the features the renderer needs, not on the
    known-bad list. Otherwise `basic`; `--renderer basic|effects|auto`
    overrides. A failure after the choice is an error, not a silent change.
  - *What it renders:* the cells and effects in an FP16 target in linear
    Rec.2020 (`Color`'s own space, 1.0 = SDR white).
  - *The swap chain* (flip model, waitable): when the display is in HDR mode,
    either `DXGI_FORMAT_R10G10B10A2_UNORM` with
    `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` (HDR10: the output stage
    PQ-encodes the Rec.2020 values, no matrix) or `R16G16B16A16_FLOAT` with
    `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709` (scRGB: a matrix to Rec.709,
    negatives allowed in the chain, 1.0 -> sdr_white_nits / 80). HDR10 is the
    first choice since it needs no conversion of gamut; scRGB if the 10-bit
    chain bands in dark gradients. On an SDR display: an 8-bit sRGB chain with
    the tone-mapped output. No static HDR metadata is set (`SetHDRMetaData`);
    the display's own tone mapping is left alone.
  - *The display:* `IDXGIOutput6::GetDesc1` for the output's colour space and
    its min/max luminance -> `DisplayInfo::hdr` (`available` is
    `ColorSpace == G2084_P2020`, `peak` is `MaxLuminance` over the SDR white,
    `gamut` from the primaries). The SDR white level is the user's slider,
    from `DisplayConfigGetDeviceInfo` with `DISPLAYCONFIG_SDR_WHITE_LEVEL`
    (1000 = 80 nits); it sets what 1.0 is in nits at the PQ encode. Read at
    start, and again on `WM_DISPLAYCHANGE`, a move to another monitor, and
    when `IDXGIFactory1::IsCurrent` says no; the change event reaches the
    application, and the output stage (swap-chain rebuild included) follows
    between frames - or, in the first version, "restart to apply".
  - *Needs:* Windows 10 1709 for the HDR swap chain colour spaces and display
    queries; Windows 11 for the best SDR-white behaviour. Older Windows has
    only the `basic` renderer.
  - *Testing:* CI (WARP, forced past the probe by a test-only flag) renders
    the float output offscreen and checks values, but cannot show HDR; the
    display-query code is exercised only on a real HDR monitor.
- **canvas:** gets a Direct2D/DirectWrite backend that replaces GDI+, so the
  pixel scopes and VU meters build on Windows too. It stays SDR; in the
  `effects` renderer its images are sampled as sRGB textures and may carry a
  gain.
- **Main loop:** `MsgWaitForMultipleObjectsEx`; on Windows `Notifier` is an
  event handle, so the audio thread's wakeups, WinMM MIDI and window
  messages wake the same `WaitSet`.
- **Dialogs and menus:** native file dialogs and message boxes through
  COM; the menu bar is an `HMENU`; Cell UI's own dialogs are separate
  owned windows.
- **Accessibility:** the widget tree is kept describable, and a UI
  Automation provider (`IRawElementProviderSimple` and fragments for the
  grid) comes in the accessibility step, not the first release.
- **Libraries:** `d2d1`, `dwrite`, `d3d11`, `dxgi`, `windowscodecs`,
  `ole32`, `uuid`, `imm32`, `dwmapi` - all in mingw-w64, all part of
  Windows. The effects renderer adds nothing
  beyond Direct3D (`d3d11` or `d3d12`, and `dxgi`), plus a shader compiler at
  build time.

## What the window does with the library's API

- **Planes:** z-order, opacity and transform, with drop shadows and blur
  behind floating layers.
- **Styles:** everything the style table holds - fonts, padding, borders,
  radius, gradients, shadows.
- **Geometry and scrolling:** pixel row heights and column widths; smooth
  scrolling and playheads, through Cell UI's viewport
  (`plans/cell-ui-terminal.md`). Done for a widget with a plane of its own
  (the arrangement grid, and the outline, whose tree has a plane of its own
  under its heading and button bar): `Viewport` keeps the offset the widget
  reasons with and a shown position that follows it (`DisplayInfo::
  smooth_scroll`), the widget draws one row more than fits from the first
  one shown and calls `UIPlane::setScrollOffset()` with the fraction, and the
  plane composes as a layer (`ScrollContent`) that the window draws from a
  texture of its own, translated and clipped - a scroll step rasterizes
  nothing. A widget that draws several kinds of content on one plane
  needs a plane for the part that scrolls (the pattern editor's headings,
  with their meters, apart from its rows is the next such change). Still to
  do: pictures inside a scrolled plane (the VU meters),
  the window's frame clock instead of a fixed 16 ms, and the cost of
  re-rasterizing the rest of the frame while the transport plays. The
  arrangement grid's playhead is a `Band` (a translucent strip at a
  fractional row over a scrolled plane, not shifted with the content, drawn
  by the window as a colour rectangle): it moves through the bars by the
  transport's position inside the row, not by whole bars.
- **Spans:** canvas images become textures, keyed like the terminal's
  images, so they are uploaded only when they change and a scroll only
  moves them.
- **Dialogs:** separate OS windows with a real title bar, draggable, modal
  when asked. Their content is cells and canvas like everything else.
- **Popups:** menus, tooltips and pickers as popup windows, so they can
  leave the main window's area (Wayland's `xdg_popup` included).
- **File dialogs and message boxes:** native (portal-backed on Linux).
- **Menus:**
  - Windows: a real `HMENU` on the main window.
  - Linux: no native menu bar exists outside a toolkit, so it is drawn to
    look native, as popup windows with the system font, not as cells. It
    takes the desktop's colour scheme, accent and font from the desktop
    portal's settings and fontconfig. With GTK as the Linux host the menu
    bar is GTK's own, in GTK's main loop. Exporting the menu over D-Bus for
    desktops with a global menu bar (KDE Plasma's) is a cheap addition
    later.
  - macOS, if ever: `NSMenu`.
- **Icons:** rasterized at the current scale into the glyph atlas and
  tinted by the cell's foreground colour, so icons follow the theme and
  state colours.
- **Text entry:** input-method composition from the host.
- **Accessibility:** the tree goes to UI Automation on Windows and AT-SPI on
  Linux (through GTK's accessibility on Linux, our own UIA provider on
  Windows).
- **Themes:** the OS dark/light preference, high contrast and reduced
  motion are followed unless set.

## Linux: GTK 4

GTK 4, not GTK 3: GTK 3 draws custom widgets with Cairo on the CPU, while
GTK 4 renders through GSK, a GPU scene graph (its GL renderer, or Vulkan
where the GTK version defaults to it). No OpenGL or SDL of our own is
needed.

- **Drawing:** the window is one custom widget whose `snapshot()` builds
  render nodes: colour rectangles for backgrounds, Pango layouts for text
  (GSK caches glyphs in its own atlas), `GdkTexture`s for canvas images
  (uploaded once, reused while their key is unchanged), rounded clips,
  borders and outset shadows for the modern look, and blur behind floating
  layers.
- **Text:** Pango, so shaping (HarfBuzz) and font fallback (fontconfig)
  come with it. A run of cells with one style is one layout, not one per
  cell.
- **Keeping it fast:** a few thousand cells is little for GSK, but the
  nodes are built so it stays that way - each plane's nodes are kept and
  reused while the plane is unchanged (render nodes are immutable), so a
  frame rebuilds only what changed; no `append_cairo` for large areas (that
  is CPU drawing uploaded every frame); an idle UI queues no frames.
- **Pacing:** redraws and smooth scrolling follow `GdkFrameClock`, which is
  vsync-paced.
- **Escape hatch:** `GtkGLArea` for a pane that needs its own shaders, used
  only if a measurement shows GSK falls short. The HDR effects
  renderer (`plans/cell-ui-hdr.md`) is that escape hatch made a whole-frame
  one: a GPU renderer (Vulkan on Linux, Direct3D on Windows) chosen at start instead of the GSK one, by a probe.
  GTK has no API for an application's own Vulkan swap chain, so
  on Linux it takes a native child surface; whether that or an SDL3 host is
  right is the spike there.
- **Measure first:** the pattern editor through the GTK backend at 4K and
  high refresh, checked for frame time and idle CPU, before the rest is
  built on it.
- **Version:** Ubuntu 24.04 has GTK 4.14, the oldest supported.

## Rendering with SDL3 as host (not taken)

Kept for reference: neither platform uses SDL3 now. On Linux, GTK 4's GSK
and Pango replace the rendering and text notes below.

- **SDL3** handles windows, input, IME, dialogs, popups, clipboard and file
  drops on Linux (X11/Wayland) and Windows. It is in MSYS2, and is vendored
  via FetchContent where the distro has none (Ubuntu 24.04).
- **Use SDL_GPU, not raw OpenGL.** Yes, render on the GPU, but through
  SDL_GPU (Vulkan on Linux, D3D12 on Windows, Metal if macOS ever comes):
  - OpenGL is deprecated on macOS, and Windows OpenGL drivers are the least
    reliable path there.
  - Software rasterizers cover machines without a GPU (WARP on Windows,
    lavapipe on Linux), so there is no separate fallback renderer to keep.
- **Speed isn't the reason.** A few thousand cells is nothing for any GPU
  path. The point is smooth frame pacing, effects, and an idle UI that
  draws nothing (the engine side already sends nothing when idle; keep it
  that way).
- **One instanced draw for cells:** a quad per cell, glyphs from an atlas.
- **SDF shaders** for rounded rects, borders, gradients and shadows. Blur
  for floating layers is a two-pass shader on that layer's texture.
- **Pixel regions are textures,** uploaded only when changed.
- **Text:** FreeType + HarfBuzz everywhere (one stack, real shaping),
  fontconfig fallback on Linux, DirectWrite font fallback lookup on
  Windows. Bundle a UI font and a mono font (both OFL) so the look is the
  same everywhere; system fonts cover scripts they lack.
- **HiDPI:** per-monitor scale from SDL; the atlas is rebuilt per scale.

## CI and testing

- CI builds the window backend on Linux and Windows.
- Golden-image tests of the gallery app, rendered offscreen through
  software rasterizers (lavapipe on Linux, WARP on Windows), with a
  tolerance.

## Steps

1. GTK backend on Linux; Win32 backend on Windows, with canvas's Direct2D
   backend. GTK first version done (`cellui::gtk::Window`, `--ui window`):
   the whole frame rasterized with `canvasRasterizer()` at scale 1 into one
   `GdkTexture`, on a re-entrant `EventLoop` that both backends use
   (`GlibEventLoop` here). Measured under Xvfb (software rendering, 4
   cores) on a playing Live View: 15 ms a frame at 1200x760, 30 ms at
   1600x1000 - all of it the rasterizer, so the per-plane render nodes
   (below) or rasterizing only the rows that changed come first. Not done:
   render nodes, input methods, HiDPI, dialogs, menus. Backdrops are blurred
   by GSK (a blur node over the layer below, `FrameInfo::backdrops`); the
   terminal's cell and picture blur is its backend's own
   (`term::BackdropEmulation`).
2. Main window, cells, text, input, popups, dialogs, file dialogs, menus;
   `--ui window|terminal`.
3. Styles beyond the terminal: geometry, spans, borders, gradients, radius,
   shadows, smooth scrolling; `plans/modern-look.md` step 6.
4. Icons in the window and the menus.
5. The standard keymap (`plans/cell-ui.md`), the window's default, with the
   emacs keymap selectable; `docs/user-interface.md` gets its table.
6. Accessibility adapters; input methods tested with a CJK input method.
7. Start menu entry for the window in the Windows installer
   (`plans/windows-port.md`).
8. HDR output (`plans/cell-ui-hdr.md`): the effects renderer's presenter on
   Windows (swap chain, display queries, fallback) once the Win32 backend and
   the `effects` renderer exist; on Linux after the host spike.
9. Voice commands, layers in 3D and the rest of the ideas in
   `plans/cell-ui.md`, as wanted.
