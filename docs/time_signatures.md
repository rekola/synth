# Time signatures and bars

## 1. Rows, beats and bars

A row is a sixteenth note. A time signature n/d is stored as its numerator
and denominator and gives whole rows:

- rows per bar = n x 16 / d
- rows per beat = 16 / d

The denominator is therefore 1, 2, 4, 8 or 16, and the numerator at most 32.
Anything else (`2/3`, `100/4`) is refused wherever a signature is typed.

| Signature | Rows per bar | Rows per beat |
|---|---|---|
| 2/4 | 8 | 4 |
| 3/4 | 12 | 4 |
| 4/4 | 16 | 4 |
| 5/4 | 20 | 4 |
| 6/8 | 12 | 2 |
| 7/8 | 14 | 2 |
| 12/8 | 24 | 2 |

3/4 and 6/8 are both 12 rows and differ in the beat only: the metronome and
the beat accents fall every 4 rows in 3/4 and every 2 in 6/8. A beat is
always a plain division of the bar; there is no grouping such as 3+2 or
2+2+3.

## 2. The song's time signature

A song has one time signature, 4/4 unless it says otherwise, saved as an
attribute of `<song>`:

```xml
<song tempo="90" timeSignature="3/4" ...>
```

Nothing is written for 4/4.

`set-time-signature` (also in the Song menu as *Set Time Signature...*) asks
for it; the prompt is filled with the current one, so Enter alone changes
nothing. The arrangement counts its bars in it from row 0: the arrangement
grid (one row per bar), the bar and beat accents in Arrangement view, where
clips and stops are placed, how an arrangement recording is rounded to bars,
copy-to-clip rounding in Arrangement view, and the arrangement's length. The
info line shows the signature in force next to the tempo.

Changing it moves no content: arrangement content is positioned by absolute
row, so only how rows are grouped into bars changes, never when anything
sounds.

## 3. The running signature

The transport has its own idea of the bar, used for everything that happens
"on the next bar": queued Session launches, stops and returns to the
arrangement, the ZBxx pattern break (which jumps to a row of the next bar),
the metronome, and the quantization and length of Session takes.

By default that is the song's. Launching a scene that has a time signature
sets the **running signature**: the transport counts bars of that signature
from the launch bar (its *origin* row) onward. Rows before the origin keep
the song's bars, so the launch bar itself is a bar start of both. The song's
own signature is untouched, so a scene never re-bars the arrangement.

The running signature ends when:
- another scene with a time signature launches, which replaces it; or
- Back to Arrangement is used for every track (`back-to-arrangement`), which
  hands the bars back to the song's on the next bar, or at once from a
  stopped transport.

It stays through stopping the scene's clips, stopping the transport and
single-clip launches. It is **saved** with the song, with its origin:

```xml
<song ... transportTimeSignature="3/4" transportBarOrigin="112">
```

and is active again when the song is loaded.

A scene without a signature leaves the running one alone. Such a scene is
shown and edited in the running signature, which is also what it plays in.

## 4. Which bars each part uses

| The song's bars | The running bars (else the song's) |
|---|---|
| Arrangement grid and its bar numbers | Applying queued Session changes on the bar |
| Arrangement-view row accents | Pattern break (ZBxx) |
| Placing clips and stops into the arrangement | Metronome click and accent |
| Recording into the arrangement | Session take quantization and length |
| Rounding for copy-to-clip in Arrangement view | Transport position display |
| Arrangement length | Info line signature |

Session view's pattern editor accents come from the scene itself: its own
signature, else the running one.

## 5. Timing

A launched scene's tempo and time signature are applied by the audio thread,
together with its clips. The press sends one event with the scene's tempo and
signature next to the clip launches; the audio thread holds it until the bar
line (or the first row played, from a stopped transport) and then, in the same
row, starts the clips, changes the tempo and starts counting bars in the new
signature. A press from the terminal and from a Launchpad take exactly the
same path (`SessionPlayer::launchScene()`), and the delay between a press and
the UI noticing it makes no difference as long as the press is before the bar.

The UI's own copies - the song tempo it shows and saves, the running
signature it quantizes takes by - are updated from the next snapshot the audio
thread sends after the change, so they can trail it by one frame (about
16 ms). Nothing the audio thread does reads them. A tempo edited since the
launch is not overwritten by an older snapshot, and an unrelated edit does not
put a scene's tempo back.
