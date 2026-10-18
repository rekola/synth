#!/usr/bin/env python3
"""Runs `synth --headless --null-audio --autoplay [SONG]` for a few seconds,
asks it to stop the way Ctrl-C would, and checks that it played and exited
cleanly. Usage: smoke_headless.py BINARY SONG|- [SECONDS]
"-" gives no song, so the default one is used."""
import os
import signal
import subprocess
import sys
import time


def main():
    binary, song = sys.argv[1], sys.argv[2]
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 8
    kwargs = {}
    if os.name == "nt":
        # Ctrl-Break goes to the child's own process group only.
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    command = [binary, "--headless", "--null-audio", "--autoplay"]
    if song != "-":
        command.append(song)
    proc = subprocess.Popen(command,
                            stderr=subprocess.PIPE, text=True, **kwargs)
    time.sleep(seconds)
    proc.send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
    try:
        _, log = proc.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()
        _, log = proc.communicate()
        print(log)
        sys.exit("synth did not stop when asked")
    print(log)
    if "Playing" not in log:
        sys.exit("synth never started playing")
    if proc.returncode != 0:
        sys.exit("synth exited with status %d" % proc.returncode)


main()
