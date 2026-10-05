# Scenes: name, tempo and time signature

This describes what a scene carries, how it is typed in, and exactly what
happens when one launches. Bars, markers and the running signature are in
time_signatures.md.

## 1. What a scene is

A scene is a row of the clip grid: clip number *k* of every track's clip
list. There is no scene object holding clips; the row exists because the
tracks' lists line up by position. What a scene does own is a small record
stored **by position** (scene 0, 1, 2, ...), like a track's clip list:

| Field | Meaning | Empty value |
|---|---|---|
| name | Free text shown in the Master column. Has no effect on playback. | `""` |
| tempo | bpm, 20-300 | 0 (none) |
| time signature | numerator / denominator | numerator 0 (none) |

Saved as `<scenes><scene name="Waltz" tempo="90" timeSignature="3/4"/>...</scenes>`,
one `<scene>` per position in order, no index attribute. Trailing scenes with
nothing set are not written. Clip rows are never inserted or removed:
deleting a clip leaves an empty slot, so scene positions stay aligned with
their records. A scene keeps its name, tempo and signature even when every
slot in it is empty.

## 2. Typing it in

In the clip grid, put the cursor on a scene slot in the **Master** column
and press **F2**. The slot shows `▸ name ♩tempo n/d` (tempo and signature at
the right; a long name is cut to fit).

The text you type is split by `scenename::extract()` (`src/model/SceneName.h`):

| Typed | Effect |
|---|---|
| `Waltz 90 BPM` | name `Waltz`, tempo 90 |
| `90bpm`, `90 bpm` | tempo 90 (any case, space optional) |
| `3/4`, `6 / 8` | time signature (spaces around `/` allowed) |
| `Waltz 3/4 90 BPM` | all three |
| `0 BPM` or `- BPM` | clears the tempo |
| `0/4` | clears the time signature |
| `xyz` (no tempo or signature in it) | renames only; tempo and signature are kept |

The tempo and signature are removed from the name, so they cannot go out of
step with the text. Text that does not read as one stays in the name: a
tempo outside 20-300, more than 3 digits, a signature whose denominator is
not 1, 2, 4, 8 or 16, or a numerator above 32 (`2/3`, `100/4`).

Which signatures are possible, and how they become rows, is in
time_signatures.md.

## 3. When a scene launches

A scene launches from the Master column slot (Enter or click), the
`launch-scene` command, or a Launchpad scene button. The tracks' clips are
queued for the next bar as always. Then:

- **Tempo.** If the scene has one, it becomes the **song tempo**, the same as
  using the tempo commands: it is stored in the song and saved with it.
- **Time signature.** If the scene has one, it becomes the transport's
  **running signature** (time_signatures.md, section 3), saved with the song.
- **Timing.** With the transport playing, both take effect on the bar the
  clips launch on, with the lag described in time_signatures.md section 5.
  With the transport stopped they apply immediately, and the running
  signature's bars are counted from where the transport stands.
- A scene with no tempo or no signature leaves the running one **unchanged**.
- Launching a scene replaces any tempo or signature still waiting from an
  earlier launch.
- Launching a single clip never touches the tempo or signature.

Using Back to Arrangement for every track also hands the bars back to the
arrangement's markers; the tempo stays, as the arrangement has no tempo
changes of its own.

## 4. The rhythm library

Each rhythm in the Library carries its time signature (Waltz and Jazz Waltz
3/4, Six-Eight 6/8, Five-Four 5/4, Seven-Eight 7/8, Slow Rock and Shuffle
Blues 12/8, March, Polka and Merengue 2/4, the rest 4/4), and its length is a
whole number of bars of it.

Add to Song puts the rhythm into a new clip at the end of the chosen track's
clip list, so it lands in the next free scene row. If that scene has no time
signature of its own, it gets the rhythm's. A scene that already has one
keeps it, so a signature you typed, or one the scene's other clips rely on,
is never overwritten. Nothing is set for the tempo, which a rhythm does not
carry. A swung rhythm sets the song's swing, as before.

Launching that scene then switches the transport to the rhythm's bars, so a
waltz scene followed by a rock scene goes 3/4, then 4/4, with no typing.

## 5. Design decisions

Where Ableton Live has a behaviour, this follows it. Live's behaviour here is
from recollection, not verified.

1. **Scene values outlast the scene.** Live's tempo and time signature are
   global controls: a scene launch sets them and nothing puts them back when
   the scene's clips stop. The same here. The one thing that hands the bars
   back is Back to Arrangement for every track, which in Live re-enables the
   arrangement's own automation.
2. **Both are saved.** The tempo is the song tempo. The running signature is
   saved with its origin row, and the arrangement's own bars are separate (its
   markers), so a scene's signature never re-bars the arrangement.
3. **A scene without a signature plays in, and is drawn in, the running
   bars.** Live scenes without a value leave the global one alone.
4. **A scene is its position.** Clip rows are never inserted or removed, so a
   record cannot drift from its row. An insert-scene or delete-scene command
   would have to move the records with the clips.
5. **The arrangement has time signature markers but no tempo changes.** Live
   has tempo automation too; here the arrangement plays at the song tempo.

## 6. Limitations

- Per-scene swing (swing is song-wide; Live's groove is per clip).
- Anything per scene beyond name, tempo and signature.
- Tempo changes inside the arrangement.
- Scene tempo and signature on the Launchpad; they are set from the
  terminal.
- Tempo and the running signature are applied by the UI side, up to a frame
  after the bar line (time_signatures.md section 5).
