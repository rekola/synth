# User interface

Everything the program does is a named command (`kill-region`, `launch-scene`,
`undo`, ...). Every command can be run by name from the
[command palette](#command-palette), most are in the menus, and a keymap binds
keys to them.

There are two keymaps, and a display uses one of them: the terminal the
terminal keymap, the window the desktop one. The command names are the same in
both. The terminal keymap is based on Emacs's keybindings (mark and point, kill
and yank, M-x, the C-x prefix), but it is this program's own and not Emacs's:
it adds chords Emacs has no counterpart for. It is used in the terminal because a terminal can't send many of the
key combinations the usual desktop shortcuts rely on (most Ctrl+Shift chords,
and Ctrl-C, Ctrl-Z and Ctrl-S belong to the terminal itself), while Emacs's
prefix keys work in any terminal. The desktop keymap follows the usual desktop,
DAW and tracker conventions. A binding that has not been given a desktop chord
works in both. The commands that have one so far:

| Command | Desktop chord | Terminal chord |
| --- | --- | --- |
| command palette | Ctrl+Shift+P | M-x |
| `undo` | Ctrl+Z | C-x u, C-/ |
| `undo-redo` | Ctrl+Shift+Z, Ctrl+Y | C-M-_ |
| `kill-region` (cut) | Ctrl+X | C-w |
| `kill-ring-save` (copy) | Ctrl+C | M-w |
| `yank` (paste) | Ctrl+V | C-y |
| `rename` (scene, clip or track, in the clip grid) | Ctrl+R | C-c r |
| `add-sample-track` | Ctrl+T | C-c t |
| `add-instrument-track` | Ctrl+Shift+T | C-c T |
| `add-group-track` | Ctrl+G | C-c g |
| `add-percussion-track` | Ctrl+Shift+D | C-c d |
| `keyboard-quit` (cancel the selection) | Escape | C-g |
| `mark-whole-buffer` (select all) | Ctrl+A | C-x h |
| `toggle-stop-button` (clip grid) | Ctrl+E | |
| `stop-track-clips` (the cursor track's clips, not the others) | Ctrl+Enter | |
| `back-to-arrangement` | F10 | F10 |
| `toggle-record-arm` (record) | F9 | F9 |
| `quantize-clip` (the clip under the cursor in the clip grid; in the pattern editor the same chord is `quantize-region`, below) | Ctrl+U | |
| `quantize-region` (the pattern editor's selection) — **not built yet**, chord reserved | Ctrl+U | |
| `duplicate` — **not built yet**, chord reserved | Ctrl+D | |
| `double-clip-length` — **not built yet**, chord reserved | Ctrl+Up | |
| `halve-clip-length` — **not built yet**, chord reserved | Ctrl+Down | |
| `capture-midi` — **not built yet**, chord reserved | Ctrl+Shift+C | |
| `find-file` | Ctrl+O | C-x C-f |
| `save-buffer` | Ctrl+S | C-x C-s |
| `save-buffer-as` | Ctrl+Shift+S | C-x C-w |
| `save-buffers-kill-terminal` (quit) | Ctrl+Q | C-x C-c |

The desktop chords follow Ableton Live's shortcuts where this program has the
same thing; an empty Terminal cell means the Emacs keybindings it is based on have no obvious counterpart, so the
chord is left free. `toggle-track-collapse` therefore has its chord (Ctrl+Shift+C) on
the terminal keymap only, as Capture MIDI has it on the desktop one.

Ctrl+X is cut there, so the `C-x` prefix sequences are terminal-only; the buffer
commands (`next-buffer`, `kill-buffer`, ...) have no desktop chord yet and are
in the menus and the command palette. What the terminal can't do is listed
under [Terminal](#terminal) at the end.

## Command palette

M-x (Meta-x: the Alt key with x, or Escape and then x) on the terminal keymap and
Ctrl+Shift+P on the desktop one open the command palette, which runs any
command by name. It sits on the last rows of the
screen, where the status line is: the query on the last row (`M-x ` or `> `),
the selected command's description above it, and the matching commands above
that, best match first. The list is a raised panel; the description (quiet and
italic) and the query are on the display's own background. The status line is
still used for messages and for prompts that ask for a name.

- Typing narrows the list by fuzzy matching: the letters in order, not
  adjacent (`tgv` finds `toggle-view`); an exact name ranks first, then a
  prefix, then matches at the start of words, then scattered letters.
- Tab completes what is typed as far as every command starting with it agrees,
  the way Emacs completes a command name; `[No match]` shows when none does. It never runs
  anything.
- Each row shows the command's keybinding in the display's keymap.
- Up/Down or C-n/C-p move, Enter or a click runs the selected command, Escape,
  C-g or a click outside cancels.

The palette takes M-x and Tab completion of the name from Emacs, and from the
command palette of Sublime Text the chord Ctrl+Shift+P, matching the typed
letters in order, and showing each command's keybinding and description. There is no minibuffer: the status line only shows
messages and asks for names (a file, a buffer, a new name).

## Terminal keymap

The rest of this page gives the terminal keymap, unless it says otherwise. Mark and point selection, kill and yank, M-x for any named command, ESC as the
Meta prefix, C-x C-c to quit and C-k to kill a row. Each open song is a buffer,
as in Emacs, and `next-buffer` and `previous-buffer` switch between them. An
operation that is a copy or a move is done with the kill ring rather than with
a command of its own, so there is no "duplicate" or "delete" command for clips.

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
instead, by view: in Live View every clip of every track, placed or not; in
Arrangement view every track's background pattern and the clips the
arrangement places (a clip placed twice is one pattern, acted on once). Any
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
digits of a velocity or delay, the characters of a command, and typed notes
(a chord counts as one, an off or a delete as one), run on from one cell to the
next, undo as one step. The run ends when you move the cursor or press any
other command, edit something else, pause for two seconds, or reach 20 edits.

The keys of a chord are one step, however long they are held (on a terminal
that reports key releases; elsewhere, keys typed within about 0.1 s of each
other). Held keys behave alike in both views: the cursor
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

In a song whose scale is otonal or utonal, those scales' notes need no
guessing: each has one exact ratio to the key (otonal 1/1 7/6 5/4 4/3 3/2 5/3
7/4, utonal 1/1 8/7 6/5 4/3 3/2 8/5 12/7). A song in either scale has both, the
11 notes they make together in 31 and 53-EDO (where they share a step in 12 and
19-EDO, the song's own scale has it). Every note on one of them gets its
ratio's correction whatever its chord, so any mix and voicing of the two (C E G
against C E♭ G, F A C E, a chord without the key) comes out pure. They also
sound that way as they are played - pads, MIDI and keyboard notes - and a
recorded or typed note of them is written with its correction. Any other note
is tuned as below, against the exact ratio when its chord's lowest note is a
scale note.

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

Select all first (C-x h) to tune every clip (Live View) or the arrangement
(Arrangement view); a clip's notes are tuned against the key, so one tuning
fits every place it is put. `clear-tuning-correction-region`
removes the corrections again, whatever put them there. Transposing the region
gives each tuned note the correction for its new pitch, and transposing the
whole song moves the key with the notes when no pitched note is left outside
the selection, so the corrections stay as they were; otherwise the key stays
and the notes are retuned against it. Changing the song's scale
(`set-song-scale`) retunes every tuned note of the song for the new scale, in
the same undo step; untuned notes stay untuned.
`undo` takes any of these back. Not on the Launchpad.

## Humanize

`humanize-region` (M-x or the Edit menu, no key binding) loosens the region's
notes: each sounding note's velocity moves by up to ±12 and its delay later by
up to 32/255 of a row. Offs and aftertouch are left alone, percussion is
included, and the mark stays, so repeated runs keep adding variation. Every
run draws new values; `undo` takes a run back. Not on the Launchpad.

## Other clip commands

Available from M-x and the Clip menu: `launch-clip`, `launch-scene`,
`stop-all-clips`, `back-to-arrangement`, `track-back-to-arrangement`,
`quantize-clip`, `toggle-stop-button`, `copy-to-clip` (new clip from the
selection) and `merge-clip-to-background`.

## Commands that only a controller uses

A command that only a pad controller dispatches, and that nothing else
registers, is named with a `pad-` prefix (`pad-next-track`, `pad-prev-track`).
A pad gesture whose keyboard equivalent is copy, kill or yank gets no command
of its own: the device calls the underlying function directly. That is why the
Launchpad's delete never touches the clipboard.
See `launchpad.md` for the gestures.

## Terminal

### Limitations

The terminal interface does everything the program does, with these limits:

- **Key releases.** Only terminals with the Kitty keyboard protocol (kitty,
  foot, WezTerm, Ghostty) report a key going up. Elsewhere keyboard note entry
  can't tell how long a note is held. Keys typed within about 0.1 s of the first
  key of a chord still go on one row, in successive note columns (a note
  already in the chord starts a new one, so a held key's repeats each get a
  row); the Launchpad and MIDI input are unaffected.
- **Key combinations.** Without that protocol many combinations arrive as the
  same byte (C-_ and C-/, for example) or not at all (most Ctrl+Shift chords),
  so some commands have fewer bindings there, and M-x is always the way in.
- **Pixel graphics.** Terminals with Kitty graphics or Sixels draw the spectrum
  analyzer and the DirAC heatmap as real pixels; the rest get braille cells.
- **Color.** A terminal without true color (xterm) is limited to the
  256-color palette.
- **Motion.** Everything moves in whole cells: scrolling, playheads and fades
  step rather than glide.

### Supported terminals

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

### Setup

#### xterm

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
