# Cell UI, phase 2: the window

The graphical backend of Cell UI (`plans/cell-ui.md`). It starts once phase 1
(`plans/cell-ui-terminal.md`) has separated the UI: the app then talks only
to `cellui`, and this phase adds a backend without touching the widgets.
Linux (X11/Wayland, whenever a display server is running) and Windows; the
terminal stays selectable (`--ui window|terminal`, the default chosen by
whether a display is available).

## Host (to be decided first)

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

Recommendation: **Qt 6 as the host**, using only its LGPL modules (Core,
Gui, Quick, and Widgets only for the menu bar and dialogs if Quick's aren't
enough). The biggest risk of a custom toolkit is the long tail of
accessibility, IME, fonts, translation and platform menus, and Qt removes
most of it, leaving us the part that is actually ours: the cell grid and its
look. The cost is size and build weight, not a second UI. Keep the Cell UI
API free of Qt types, so the host can still be swapped for SDL3 later.

Before committing to either, a short spike: the pattern editor drawn
through a minimal window backend on each host (cells, glyph atlas, one
shadowed popup, a menu, a modal dialog), compared on Linux and Windows for
looks, input latency, idle CPU and installer size.

## What the window does with the library's API

- **Planes:** z-order, opacity and transform, with drop shadows and blur
  behind floating layers.
- **Styles:** everything the style table holds - fonts, padding, borders,
  radius, gradients, shadows.
- **Geometry and scrolling:** pixel row heights and column widths; smooth,
  vsync-paced scrolling and playheads.
- **Spans:** canvas images become textures, uploaded only when they change.
- **Dialogs:** separate OS windows with a real title bar, draggable, modal
  when asked. Their content is cells and canvas like everything else.
- **Popups:** menus, tooltips and pickers as popup windows, so they can
  leave the main window's area (Wayland's `xdg_popup` included).
- **File dialogs and message boxes:** native (portal-backed on Linux).
- **Menus:**
  - Windows: a real `HMENU` on the main window (or the host's native menu
    bar).
  - Linux: no native menu bar exists outside a toolkit, so it is drawn to
    look native, as popup windows with the system font, not as cells. It
    takes the desktop's colour scheme, accent and font from the desktop
    portal's settings and fontconfig. Not GTK: a GTK menu needs a GTK
    parent widget and GTK's main loop, on Wayland it cannot attach to
    another toolkit's surface, and it would look like GNOME on KDE anyway.
    Exporting the menu over D-Bus for desktops with a global menu bar (KDE
    Plasma's) is a cheap addition later. With Qt as the host, its menu bar
    covers this.
  - macOS, if ever: `NSMenu`.
- **Icons:** rasterized at the current scale into the glyph atlas and
  tinted by the cell's foreground colour, so icons follow the theme and
  state colours.
- **Text entry:** input-method composition from the host.
- **Accessibility:** the tree goes to UI Automation on Windows and AT-SPI on
  Linux (through Qt's accessibility with Qt as host, AccessKit with SDL3).
- **Themes:** the OS dark/light preference, high contrast and reduced
  motion are followed unless set.

## Rendering with SDL3 as host

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

1. Host spike (above) and the decision.
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
8. Voice commands, layers in 3D and the rest of the ideas in
   `plans/cell-ui.md`, as wanted.
