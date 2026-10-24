# Headless mode

`synth --ui headless` runs without a terminal UI: it opens the song, plays it
through the audio device and keeps a connected Launchpad working. It is meant
for unattended setups (installations, escape rooms) and for watching debug
output from a plain shell.

```sh
./build/synth --ui headless --autoplay songs/demo3.xml
```

Status and error lines go to stderr with a timestamp (`[12:00:01.234]
Playing`). Ctrl-C, SIGTERM and SIGHUP end the run cleanly (voices released,
Launchpad LEDs cleared) with exit status 0.

## Options

| Option | Meaning |
|---|---|
| `--ui headless` | No terminal UI; status lines on stderr. |
| `--autoplay` | Start the transport once running (headless only). |
| `--null-audio` | No sound device: blocks are discarded at the pace of the clock. For machines without audio hardware and for tests. |
| `--daemon` | `--ui headless`, detached from the terminal. |
| `--log-file F` | With `--daemon`: where stderr goes (appended; default `/dev/null`). |
| `--pid-file F` | With `--daemon`: write the daemon's pid here. |
| `--playback-device N` / `--capture-device N` / `--midi-input N` | Use this audio output / audio input / MIDI source for this run instead of the saved choice. |
| `--list-devices` | Print what can be chosen, with the option for each, and exit. |

Headless mode has no pickers; it uses the saved choice (made from the terminal
UI) unless one of the options above overrides it. See `docs/devices.md`.

`--daemon` forks before any thread or audio handle exists. The launching
process waits until the daemon is up and then exits 0; if startup fails (for
example the song doesn't load) it exits 1 and the reason is in the log file.
The working directory is left unchanged, so relative song paths still work.
Stop a daemon with `kill $(cat F)`.

If no song is given, `songs/welcome.xml` is used as in the terminal UI.

## Windows

Windows builds have no terminal UI, so `--ui headless` is how to run the program
there (`synth.exe --ui headless --autoplay songs\demo3.xml`). Audio goes through
WASAPI and MIDI and a Launchpad through WinMM. Ctrl-C and Ctrl-Break end the
run cleanly. `--daemon` is not supported. With no pickers, choose devices with
`--list-devices` and the `--playback-device`/`--capture-device`/`--midi-input`
options, or write the choice to `%APPDATA%\synth\devices.conf` (the format
is in `docs/devices.md`). Audio and MIDI have not been tried on real hardware
yet; a Launchpad is not recognised on Windows until its port names are known.

## Limitations

Anything that needs the terminal UI is not available: editing, M-x and the
keyboard, and other peripherals. The "cursor" headless mode uses is the
song's current track plus the transport position, both held outside the UI.

## Testing

`tools/smoke_headless.py` runs headless mode with `--null-audio` for a few
seconds, stops it as Ctrl-C would and checks that it played; the Windows CI
job uses it.

`tools/e2e/verify_headless.py` runs headless mode against `build/synth`
using ALSA's `null` PCM, so it needs no sound card.
