# Headless mode

`synth --headless` runs without a terminal UI: it opens the song, plays it
through the audio device and keeps a connected Launchpad working. It is meant
for unattended setups (installations, escape rooms) and for watching debug
output from a plain shell.

```sh
./build/synth --headless --autoplay songs/demo3.xml
```

Status and error lines go to stderr with a timestamp (`[12:00:01.234]
Playing`). Ctrl-C, SIGTERM and SIGHUP end the run cleanly (voices released,
Launchpad LEDs cleared) with exit status 0.

## Options

| Option | Meaning |
|---|---|
| `--headless` | No terminal UI; status lines on stderr. |
| `--autoplay` | Start the transport once running (headless only). |
| `--daemon` | `--headless`, detached from the terminal. |
| `--log-file F` | With `--daemon`: where stderr goes (appended; default `/dev/null`). |
| `--pid-file F` | With `--daemon`: write the daemon's pid here. |

`--daemon` forks before any thread or audio handle exists. The launching
process waits until the daemon is up and then exits 0; if startup fails (for
example the song doesn't load) it exits 1 and the reason is in the log file.
The working directory is left unchanged, so relative song paths still work.
Stop a daemon with `kill $(cat F)`.

If no song is given, `songs/welcome.xml` is used as in the terminal UI.

## What works

- Playback, Session view clip launching and everything else the engine does.
- Launchpad input and LEDs (the same pad/button handling as the terminal UI).
- MIDI input: notes (with release, note and channel pressure) play live on
  the current track, in the song's tuning, one voice slot per held note;
  respects the track's Monitor setting.
- Sample recording from the audio input (armed, threshold-triggered and
  Session view takes), shared with the terminal UI.

Not available: anything that needs the terminal UI (editing, M-x, the
keyboard), writing MIDI notes into patterns (the terminal UI does that at its
edit cursor, which headless mode doesn't have), and other peripherals.

## Testing

`tools/e2e/verify_headless.py` runs the checks above against `build/synth`
using ALSA's `null` PCM, so it needs no sound card.
