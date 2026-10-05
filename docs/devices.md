# Audio and MIDI devices

The audio output, the audio input (used for sampling and live monitoring)
and one MIDI input can be chosen while the program runs. The choice is saved
and used again the next time.

## Choosing

Open the **Devices** menu, or run these with M-x:

| Command | Chooses |
|---|---|
| `select-playback-device` | the audio output |
| `select-capture-device` | the audio input |
| `select-midi-input` | the MIDI source notes are played from |

Each asks in the minibuffer and shows the current choice. Tab completes the
name (case is ignored), Enter takes an exact name or a unique start of one,
and an empty answer changes nothing. The list is read each time, so a device
plugged in a moment ago is there.

"System default" follows whatever the system currently treats as the default.
"None" for MIDI connects nothing; a source connected from outside the program
still works.

A change takes effect at once, without stopping playback. The output is
switched in the audio thread, so the sound may glitch for an instant. Some
changes are refused, with the reason on the status line, and leave things as
they were:

- The input can't change while recording, armed for threshold recording, or
  monitoring, since that stream is in use.
- The output can't change to a device that can't run at the sample rate and
  block size already in use. Restart with `--playback-device` to use it.
- A device that no longer exists is refused.

## On the command line

```sh
./build/synth --list-devices
./build/synth --playback-device "pw:alsa_output.usb-Foo" --capture-device "pw:alsa_input.usb-Foo"
./build/synth --midi-input "USB Keyboard:USB Keyboard MIDI 1"
```

`--list-devices` prints every choice with the exact option to select it.
These options apply to that run only and never change the saved choice.

## What is saved

`$XDG_CONFIG_HOME/synth/devices.conf`, or `~/.config/synth/devices.conf`:

```
capture = pw:alsa_input.usb-Foo
playback = pw:alsa_output.usb-Foo
midi_input = USB Keyboard:USB Keyboard MIDI 1
```

It is a machine setting, not part of any song. An empty value is the system
default (or no MIDI source). A saved device that isn't there at startup is
reported on the status line and the default is used instead; the saved
choice is kept, so the device is picked up again once it returns. A MIDI
source that is missing is connected as soon as it appears.

## Device names

- `pw:<name>` is a PipeWire node, by its `node.name`. These are stable across
  restarts and reboots, unlike numeric ids.
- Anything else is an ALSA PCM name such as `hw:1,0`. Only useful without
  PipeWire; with it, cards are normally held by PipeWire and the nodes should
  be used instead.
- MIDI is `<client name>:<port name>` of an ALSA sequencer port. Launchpads
  are left out of the list, since they have their own connection.

## PipeWire

Listing the inputs and outputs needs the optional `libpipewire-0.3-dev` when
building (detected automatically; `-DSYNTH_ENABLE_PIPEWIRE=OFF` skips it).
Without it the lists fall back to ALSA's own, which on a PipeWire system hold
little more than the default.

Selection itself doesn't need the library. The PCM is opened through ALSA's
`pipewire` plugin (`pipewire-alsa`) with the node named in a private config,
so buffering, latency measurement and error recovery are the same as for any
other device. If the plugin isn't installed, opening a `pw:` name fails and
the default is used.

Not supported: recording a sink's monitor (the system's own output) - the
server falls back to the default input instead, so sinks aren't offered.
Capture is always one channel; the server mixes a stereo input down.
