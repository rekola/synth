#!/usr/bin/env python3
"""Drive the audio device pickers through a pty against a real PipeWire
daemon: select-playback-device / select-capture-device switch the live
streams and save the choice, and a fresh `synth` starts on what was saved.

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

    nodes = [
        ("e2e_sink_a", "E2E Sink A", "Audio/Sink"),
        ("e2e_sink_b", "E2E Sink B", "Audio/Sink"),
        ("e2e_source_a", "E2E Source A", "Audio/Source/Virtual"),
    ]
    keeper = subprocess.Popen(["pw-cli"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
    for name, desc, cls in nodes:
        keeper.stdin.write(
            "create-node adapter { factory.name=support.null-audio-sink "
            f"node.name={name} node.description=\"{desc}\" media.class={cls} "
            "audio.position=[FL FR] }\n")
    keeper.stdin.flush()
    time.sleep(1.0)

    config_dir = tempfile.mkdtemp(prefix="synth_e2e_cfg_")
    conf = os.path.join(config_dir, "synth", "devices.conf")
    pid = None
    try:
        pid, scr = start(config_dir)

        mx(scr, "select-playback-device")
        check("the output picker shows the current device", "Audio output (current: System default)" in scr.dump(), scr)
        scr.send(b"E2E Sink B\r")
        check("choosing an output reports it", wait_until(scr, lambda: "Playback device: pw:e2e_sink_b" in scr.dump()), scr)
        check("the playback stream moved to that node",
              wait_until(scr, lambda: synth_playback_target() == "e2e_sink_b"))
        check("the choice is saved", os.path.exists(conf) and "playback = pw:e2e_sink_b" in open(conf).read())

        mx(scr, "select-capture-device")
        check("the input picker lists the new source", "Audio input (current: System default)" in scr.dump(), scr)
        scr.send(b"E2E Source A\r")
        check("choosing an input reports it", wait_until(scr, lambda: "Capture device: pw:e2e_source_a" in scr.dump()), scr)
        check("both choices are saved", "capture = pw:e2e_source_a" in open(conf).read()
              and "playback = pw:e2e_sink_b" in open(conf).read())

        mx(scr, "select-playback-device")
        scr.send(b"No Such Device\r")
        check("an unknown name is refused", wait_until(scr, lambda: "No such device" in scr.dump()), scr)
        check("a refused name leaves the stream where it was", synth_playback_target() == "e2e_sink_b")

        stop(pid, scr)
        pid = None

        pid, scr = start(config_dir)
        check("a new run starts on the saved output",
              wait_until(scr, lambda: synth_playback_target() == "e2e_sink_b"), scr)
        check("a new run starts on the saved input", "Capture device: pw:e2e_source_a" in scr.dump(), scr)
    finally:
        if pid:
            os.kill(pid, 9)
        keeper.terminate()
        shutil.rmtree(config_dir, ignore_errors=True)

    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
