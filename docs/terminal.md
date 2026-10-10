# Terminal workflow

The terminal UI follows Emacs: mark and point selection, kill and yank, M-x for
any named command, ESC as the Meta prefix, C-x C-c to quit and C-k to kill a
row. Each open song is a buffer, as in Emacs, and
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
and tracks (C-SPC sets the mark). C-x h (`mark-whole-buffer`) selects the whole song
instead: every track's background pattern and every clip, in either view. Any
cursor movement or C-g ends it. Transpose, humanize and the tuning commands act
on it; cut, copy and yank say they do not. To duplicate a track's notes, select
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

## Moving a paused playhead

Once anything has played, a clip the arrangement has at the transport's row has
a playhead in Live View, paused or not. With the transport paused, the pattern
editor's cursor in such a clip is that playhead: Up and Down move it, and so
move the arrangement's position (reaching the clip's first row does not detach
it), and playing resumes from there. A launched clip's playhead moves the same
way, taking the other launched clips along. A song that has never played has no
clip playing, so there the cursor moves alone.

## Undo

`undo` (C-x u, C-_ or C-/) takes back the last edit to the song; repeating it
walks further back. `undo-redo` (C-M-_) puts back what the last undo took away,
and works only while the newest thing done is a run of undos. Any other edit
ends that run, and the next `undo` then undoes that edit (Emacs's own rule,
where an undo is itself an edit that can be undone). Moving the cursor or
scrolling is not an edit and does not end the run. Both are in the Edit menu
and M-x, and are the Launchpad's shift + Record Arm and shift + Mute.
A terminal sends C-_ and C-/ as the same single control byte (0x1f), and
Ctrl+Shift+- is how C-_ is typed in the Kitty protocol; all of them are bound.

Only the song is undone: tempo, notes, clips and placements, tracks,
instruments and bus effects. The view and the clipboard are not. The current
track moves to the track the change was on, the cursor returns to the velocity,
delay, command or note cell that changed, and the transport row moves to the row
of an arrangement note or placement (never while the song plays). A change to a clip's notes shows that clip: the
clip grid's cursor goes to its scene and, in Live View, the pattern editor to
the changed row (unless that track is playing).
Changes that follow the playing song (a scene launch setting the tempo) are
never undone; so are mute and solo, which are performance state rather than edits. A velocity or delay is one integer, not a string of characters as
in Renoise, so it is atomic: its hex digits are held in the cell (shown, but not
in the song, so playback never hears half a value) and committed together when
the cursor leaves the cell, the pattern editor loses focus or any other key is
pressed; Enter commits it and moves to the next column. A delay needs a note (or
aftertouch) to belong to, so typing one on an empty column is refused; a velocity
there makes an aftertouch. A command is text and is written as it is typed. Typed digits are amalgamated as Emacs does with typed characters: the hex
digits of a velocity or delay and the characters of a command, run on from one
cell to the next, undo as one step. The run ends when you move the cursor or
press any other command, edit something else, pause for two seconds, or reach
20 edits.

The keys of a chord are one step, however long they are held (on a terminal
that reports key releases). Held keys behave alike in both views: the cursor
stays on the row and steps once when the last key lifts; nothing starts the
transport. A live recording take is one step: its notes appear as they land, and
`undo` takes the whole take back once it has ended. Undo and redo do nothing
while a take is still recording, and they leave a clip that is playing (or
queued, or being recorded) alone: an undo that would change its notes says so
and waits until the clip is stopped.

## Just intonation

`apply-just-intonation-region` (M-x or the Edit menu, no key binding) gives
every pitched note in the region a tuning correction: the cents that move it
from its equal-tempered pitch to a just ratio (`model/JustIntonation.h`). The
correction goes in the note's fx as `+hh` or `-hh` (commands.md), and `+00`
marks a note as tuned that needs none.

A note is tuned together with the others of its chord. Per track and per
pattern (a track's background, or one clip), the notes that start in a bar,
plus the ones still held into it, are one chord, so an arpeggio is one chord
because all its steps start in the same bar. The chord's lowest note, the
bass, is tuned from the song key; every other note is tuned as its interval
above the bass. The ratio for an interval, or for a step above the key, is the
simplest one that lands within half a step (and at most 20 cents) of it, so no
limit is chosen: 31-EDO reaches 7/6 and 11/10, 12-EDO mostly stays with 5-limit
ratios (plus 7/5 and 15/14). Only primes above 13 are never used. A chord is
therefore pure against its bass, and the bass keeps the key from drifting
between chords. A ii chord in C (D F A) is 10:12:15 above its D.

What it does not do: chords are per track (a bass track and a pad track agree
on the root, both measuring it from the key, but not on the upper notes), a
chord that changes inside a bar is read as one chord, and a held note keeps the
correction it got when it started. Percussion, offs and aftertouch are skipped;
any other fx a note had is replaced, and the message says how many. The context
is read from the whole pattern, so a region that covers part of a chord still
tunes it as the whole chord, writing only the notes it covers. A run replaces
earlier corrections, so it can be repeated after editing; the mark stays.

Select the whole song first (C-x h) to tune it all. `clear-tuning-correction-region`
removes the corrections again, whatever put them there. Transposing the region
gives each tuned note the correction for its new pitch, and transposing the
whole song moves the key with the notes, so the corrections stay as they were.
`undo` takes any of these back. Terminal only.

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
