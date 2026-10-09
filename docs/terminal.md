# Terminal workflow

The terminal UI follows Emacs: mark and point selection, kill and yank, M-x for
any named command, ESC as the Meta prefix, C-x C-c to quit and C-k to kill a
row. The one addition is C-b as a second way to set the mark, since C-SPC does
not reach every terminal. Each open song is a buffer, as in Emacs, and
`next-buffer` and `previous-buffer` switch between them; this belongs to the
terminal interface only, and other interfaces will follow their own conventions. An operation that is a copy or a move is done with
the kill ring rather than with a command of its own, so there is no
"duplicate" or "delete" command for clips.

## Kill, copy and yank

| Key | Command | What it does |
|---|---|---|
| M-w | `kill-ring-save` | Copy |
| C-w | `kill-region` | Cut |
| C-y | `yank` | Paste at the cursor |
| C-g | `keyboard-quit` | Cancel the selection |

Which thing they act on depends on the focused widget.

### Pattern editor

They act on the region: the note under the cursor, or the marked block of rows
and tracks (C-SPC or C-b sets the mark). To duplicate a track's notes, select
the track, copy it, move to another track and yank.

### Clip grid (Live View)

They act on the clip under the cursor.

- **Duplicate a clip:** copy it, move to another slot (the one below, say) and
  yank. The paste is an independent copy under a fresh id and replaces what is
  in the slot, together with that clip's arrangement placements.
- **Move a clip:** cut it, move, yank.
- **Between tracks:** yank only onto a track of the same kind, because note
  values mean different things under different tunings and audio only fits
  audio.
- **Delete a clip:** cut it. Del, Backspace and C-k do the same, so a deleted
  clip can always be yanked back. On an empty slot they remove the slot's stop
  button instead, which is not clip content and never reaches the clipboard.
- A clip that is playing is not pulled out from under the playhead: its track
  stops at the next bar and the clip is removed then.

## Undo

`undo` (C-x u, C-_ or C-/) takes back the last edit to the song; repeating it
walks further back. `undo-redo` (C-M-_) puts back what the last undo took away,
and works only while the newest thing done is a run of undos. Any other edit
ends that run, and the next `undo` then undoes that edit (Emacs's own rule,
where an undo is itself an edit that can be undone). Moving the cursor or
scrolling is not an edit and does not end the run. Both are in the Edit menu
and M-x, and are the Launchpad's shift + Record Arm and shift + Mute.

Only the song is undone: tempo, notes, clips and placements, tracks,
instruments and bus effects. The cursor, the view and the clipboard are not.
Changes that follow the playing song (a scene launch setting the tempo) are
never undone. A live recording take is one step: its notes appear as they land, and
`undo` takes the whole take back once it has ended. Undo and redo do nothing
while a take is still recording.

## Humanize

`humanize-region` (M-x or the Edit menu, no key binding) loosens the region's
notes: each sounding note's velocity moves by up to ±12 and its delay later by
up to 32/255 of a row. Offs and aftertouch are left alone, percussion is
included, and the mark stays, so repeated runs keep adding variation. Every
run draws new values; `undo` takes a run back. Terminal only.

## Other clip commands

Available from M-x and the Clip menu: `launch-clip`, `launch-scene`,
`stop-all-clips`, `back-to-arrangement`, `track-back-to-arrangement`,
`quantize-clip`, `toggle-stop-button`, `copy-to-clip` (new clip from the
selection) and `merge-clip-to-background`.

## Commands that only a controller uses

A command that only a pad controller dispatches, and that nothing else
registers, is named with a `pad-` prefix (`pad-next-track`, `pad-prev-track`).
A pad gesture whose terminal equivalent is copy, kill or yank gets no command
of its own: the device calls the underlying function directly. That is why the
Launchpad's delete never touches the clipboard.
See `launchpad.md` for the gestures.

## Supported terminals

Known to work are kitty, xterm and GNOME Terminal; the rest are untested here.
The Graphics and Mouse columns come from testing another program (nanoclj) in
these terminals; xterm's lack of true color applies to synth as well.

| Terminal | Status | Graphics | Mouse | Notes |
| - | - | - | - | - |
| foot | Untested | OK | OK | Wayland only |
| kitty | Works | OK | ? | True color images, but window resizing has bugs (as of 0.26.5) |
| wezterm | Untested | OK | OK | Buggy (as of 20230712) |
| mlterm | Untested | OK | ? | |
| Konsole | Untested | OK | ? | True color images, but on HiDPI system images are upscaled |
| contour | Untested | Inline image layout doesn't work | ? | |
| xterm | Works | No true color | OK | Sixel support must be enabled in `.Xresources` (see [xterm](#xterm) below), and images have maximum size 1000x1000 |
| Black Box | Untested | Inline image layout doesn't work | ? | On HiDPI system the images are upscaled, and the terminal and the flatpak system use too much CPU time when idling. |
| Alacritty | Untested | None | ? | |
| GNOME Terminal | Works | None | ? | Sixel support is not enabled by default |
| mintty | Untested | ? | ? | Not tested yet. |

## Terminal setup

Pixel graphics (Kitty graphics or Sixels) and the Kitty keyboard protocol are
optional; the UI falls back to braille cells and ordinary key encodings. Without
the keyboard protocol a terminal never reports a key going up, so held notes and
musical chords can't be played from the keyboard there.

### xterm

xterm's defaults are poor for this UI: 16 colors, no Sixels, a small image
limit, a tiny bitmap font. Put this in `~/.Xresources`:

```
XTerm*termName: xterm-256color
XTerm*decTerminalID: vt340
XTerm*numColorRegisters: 256
XTerm*maxGraphicSize: 2048x2048
XTerm*renderFont: true
XTerm*faceName: monospace
XTerm*faceSize: 12
```

- `termName` sets `TERM`; xterm's default `xterm` has only 8/16 colors, which
  is why the UI looked 16-color everywhere but the Sixel images.
- `decTerminalID: vt340` makes xterm identify as Sixel-capable, and
  `numColorRegisters` sets the image palette size.
- `maxGraphicSize` raises the Sixel image size limit (default 1000x1000).
- `renderFont` with `faceName` selects an antialiased Xft font through
  fontconfig. `monospace` is fontconfig's alias for the system's default
  monospace font, so it is the same on every machine; name a font such as
  `DejaVu Sans Mono` to pick one. `faceSize` is in points.

Even so, xterm has no true color, so the UI is limited to the 256-color
palette there.

Reload with `xrdb -merge ~/.Xresources` and start a new xterm; running ones
keep their old settings. If `TERM` is still `xterm` inside it, check with
`echo $TERM`; a shell profile that exports `TERM` overrides the resource.
xterm does not implement the Kitty keyboard protocol, so use C-b rather than
C-SPC to set the mark.
