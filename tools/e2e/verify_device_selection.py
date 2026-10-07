#!/usr/bin/env python3
"""Drive the audio device dialogs through a pty against a real PipeWire
daemon: select-playback-device / select-capture-device open a list, choosing
an entry switches the live stream and saves the choice, cancelling changes
nothing, and a fresh `synth` starts on what was saved.

Needs a running PipeWire (with a session manager) and `pw-cli`/`pw-link`;
prints SKIP otherwise. Creates its own null sinks/source - held by one
pw-cli connection, so they vanish when the script ends - and a throwaway
XDG_CONFIG_HOME, so the real device settings are never touched."""
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as vk

SONG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "launchpad_session_test.xml")
results = []


def check(name, ok, scr=None):
    results.append(ok)
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok and scr is not None:
        print(scr.dump())


def links():
    return subprocess.run(["pw-link", "-l"], capture_output=True, text=True).stdout


def synth_playback_target():
    """The node the player's playback stream is linked to, or None."""
    lines = links().splitlines()
    for i, line in enumerate(lines):
        if line.startswith("alsa_playback.synth:output_FL") and i + 1 < len(lines):
            return lines[i + 1].split("->")[-1].strip().split(":")[0]
    return None


def wait_until(scr, pred, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        scr.pump(0.3)
        if pred():
            return True
    return False


def mx(scr, command):
    scr.send(b"\x1bx")
    scr.pump(0.4)
    scr.send(command.encode() + b"\r")
    scr.pump(0.6)


DOWN, UP = b"\x1b[B", b"\x1b[A"


def dialog_bounds(scr):
    """(top, bottom) screen rows of the open choice dialog, or None."""
    lines = scr.screen.display
    top = next((i for i, l in enumerate(lines) if "\u250c\u2500 " in l and ("Audio" in l or "MIDI input" in l)), None)
    if top is None:
        return None
    bottom = next((i for i in range(top + 1, len(lines)) if "\u2514\u2500" in lines[i]), None)
    return None if bottom is None else (top, bottom)


def dialog_open(scr):
    return dialog_bounds(scr) is not None


def dialog_row(scr, label):
    top, bottom = dialog_bounds(scr)
    for y in range(top + 1, bottom):
        if label in scr.screen.display[y]:
            return y
    return None


def selected_row(scr):
    """The dialog row drawn with the cursor colours: the one whose background
    differs from the dialog's own (read off its border). Compared rather than
    matched against an exact colour, since the emulated terminal quantises
    colours to its palette."""
    top, bottom = dialog_bounds(scr)
    left = scr.screen.display[top].index("\u250c")
    base = scr.screen.buffer[top][left + 3].bg
    for y in range(top + 1, bottom):
        if scr.screen.buffer[y][left + 4].bg != base:
            return y
    return None


def pick(scr, label):
    """Moves the dialog's selection onto `label` with the arrow keys and
    chooses it with Enter."""
    target, current = dialog_row(scr, label), selected_row(scr)
    if target is None or current is None:
        return False
    scr.send((DOWN if target > current else UP) * abs(target - current))
    scr.pump(0.4)
    scr.send(b"\r")
    return True


def click(scr, label):
    """Left-clicks the dialog entry showing `label`: SGR mouse press and
    release at its first character (1-based terminal coordinates)."""
    y = dialog_row(scr, label)
    if y is None:
        return False
    x = scr.screen.display[y].index(label)
    scr.send(f"\x1b[<0;{x + 1};{y + 1}M".encode())
    scr.pump(0.2)
    scr.send(f"\x1b[<0;{x + 1};{y + 1}m".encode())
    return True


def start(config_dir):
    os.environ["XDG_CONFIG_HOME"] = config_dir
    pid, fd = vk.spawn(SONG, view="session")
    scr = vk.Screen(fd)
    if not vk.wait_ready(scr):
        print("UI never became ready within timeout")
        os.kill(pid, 9)
        sys.exit(2)
    scr.pump(0.5)
    return pid, scr


def stop(pid, scr):
    scr.send(vk.ctrl('x'))
    scr.pump(0.3)
    scr.send(vk.ctrl('c'))
    end = time.time() + 5
    while time.time() < end:
        scr.pump(0.2)
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done:
            return
    os.kill(pid, 9)
    os.waitpid(pid, 0)


def main():
    if not (shutil.which("pw-cli") and shutil.which("pw-link")) or subprocess.run(
            ["pw-link", "-l"], capture_output=True).returncode != 0:
        print("SKIP: needs a running PipeWire and pw-cli/pw-link")
        return

    # Two sources that share a description, like two identical USB dongles. A
    # real card's location comes from its device object, which a null sink
    # doesn't have, so they are told apart by their node names here.
    nodes = [
        ("e2e_sink_a", "E2E Sink A", "Audio/Sink", ""),
        ("e2e_sink_b", "E2E Sink B", "Audio/Sink", ""),
        ("e2e_source_a", "E2E Source A", "Audio/Source/Virtual", ""),
        ("e2e_twin_a", "E2E Twin", "Audio/Source/Virtual", ""),
        ("e2e_twin_b", "E2E Twin", "Audio/Source/Virtual", ""),
    ]
    keeper = subprocess.Popen(["pw-cli"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
    for name, desc, cls, extra in nodes:
        keeper.stdin.write(
            "create-node adapter { factory.name=support.null-audio-sink "
            f"node.name={name} node.description=\"{desc}\" media.class={cls} {extra} "
            "audio.position=[FL FR] }\n")
    keeper.stdin.flush()
    time.sleep(1.0)

    config_dir = tempfile.mkdtemp(prefix="synth_e2e_cfg_")
    conf = os.path.join(config_dir, "synth", "devices.conf")
    pid = None
    try:
        pid, scr = start(config_dir)

        mx(scr, "select-playback-device")
        check("the output dialog opens with a title", dialog_open(scr) and "Audio output" in scr.dump(), scr)
        default_row = dialog_row(scr, "System default")
        check("the device in use is marked", default_row is not None and "\u25cf" in scr.screen.display[default_row], scr)
        check("the in-use device starts selected", default_row is not None and default_row == selected_row(scr), scr)
        check("the dialog lists the new output", dialog_row(scr, "E2E Sink B") is not None, scr)
        check("the sources are not offered as outputs", dialog_row(scr, "E2E Source A") is None, scr)

        scr.send(vk.ctrl('g'))
        check("C-g cancels the dialog", wait_until(scr, lambda: not dialog_open(scr)), scr)
        check("cancelling changes nothing", not os.path.exists(conf) and synth_playback_target() != "e2e_sink_b")

        mx(scr, "select-playback-device")
        check("choosing an output works", pick(scr, "E2E Sink B"), scr)
        check("choosing an output reports it", wait_until(scr, lambda: "Playback device: pw:e2e_sink_b" in scr.dump()), scr)
        check("the dialog closes after choosing", not dialog_open(scr), scr)
        check("the playback stream moved to that node",
              wait_until(scr, lambda: synth_playback_target() == "e2e_sink_b"))
        check("the choice is saved", os.path.exists(conf) and "playback = pw:e2e_sink_b" in open(conf).read())

        mx(scr, "select-playback-device")
        chosen = dialog_row(scr, "E2E Sink B")
        check("the new output is now the marked one", chosen is not None and "\u25cf" in scr.screen.display[chosen], scr)
        scr.send(b"\r")  # Enter on the device already in use
        check("choosing the device in use closes quietly", wait_until(scr, lambda: not dialog_open(scr)), scr)
        check("and leaves the stream where it was", synth_playback_target() == "e2e_sink_b")

        mx(scr, "select-capture-device")
        check("the input dialog lists the new source", dialog_open(scr) and dialog_row(scr, "E2E Source A") is not None, scr)
        check("identical devices are told apart",
              dialog_row(scr, "E2E Twin [e2e_twin_a]") is not None and dialog_row(scr, "E2E Twin [e2e_twin_b]") is not None, scr)
        check("a device with a unique name is left plain", dialog_row(scr, "E2E Source A [") is None, scr)
        check("clicking an entry chooses it", click(scr, "E2E Source A"), scr)
        check("choosing an input reports it", wait_until(scr, lambda: "Capture device: pw:e2e_source_a" in scr.dump()), scr)
        check("both choices are saved", "capture = pw:e2e_source_a" in open(conf).read()
              and "playback = pw:e2e_sink_b" in open(conf).read())

        stop(pid, scr)
        pid = None

        pid, scr = start(config_dir)
        check("a new run starts on the saved output",
              wait_until(scr, lambda: synth_playback_target() == "e2e_sink_b"), scr)
        check("a new run starts on the saved input", "Capture device: pw:e2e_source_a" in scr.dump(), scr)

        mx(scr, "select-playback-device")
        check("the dialog opens again", dialog_open(scr), scr)
        # notcurses holds a lone Escape byte until the next one arrives (so it
        # can tell it from the start of a sequence); a second Escape releases
        # it. Nothing is sent after this - the second one stays buffered.
        scr.send(b"\x1b")
        scr.pump(0.5)
        scr.send(b"\x1b")
        check("Escape cancels it", wait_until(scr, lambda: not dialog_open(scr)), scr)
    finally:
        if pid:
            os.kill(pid, 9)
        keeper.terminate()
        shutil.rmtree(config_dir, ignore_errors=True)

    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
