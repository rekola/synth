#!/usr/bin/env python3
"""End-to-end check of `synth --headless` and `--daemon`: starts playing,
logs status on stderr, and shuts down cleanly on SIGTERM/SIGINT.

Needs no terminal and no sound card: ALSA's "default" PCM is redirected to
the `null` plugin through ALSA_CONFIG_PATH. Not part of ctest, like the
other scripts here, because it spawns the real binary.
"""
import os
import signal
import subprocess
import sys
import tempfile
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO_ROOT, "build", "synth")
SONG = os.path.join(REPO_ROOT, "songs", "demo3.xml")

failures = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)


def wait_for(path, text, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if text in open(path).read():
                return True
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    return False


def pid_gone(pid, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        time.sleep(0.1)
    return False


with tempfile.TemporaryDirectory() as tmp:
    alsa_conf = os.path.join(tmp, "asound.conf")
    with open(alsa_conf, "w") as f:
        f.write("pcm.!default { type null }\n")
    env = dict(os.environ, ALSA_CONFIG_PATH=alsa_conf, SYNTH_LAUNCHPAD_NO_HARDWARE="1")

    # Foreground: debug text on stderr, SIGTERM/SIGINT exit 0.
    for sig, name in ((signal.SIGTERM, "SIGTERM"), (signal.SIGINT, "SIGINT")):
        log = os.path.join(tmp, name + ".log")
        with open(log, "w") as err:
            p = subprocess.Popen([BINARY, "--headless", "--autoplay", SONG], env=env, stderr=err, stdin=subprocess.DEVNULL)
        check(wait_for(log, "Playing"), "%s: --headless --autoplay reports Playing on stderr" % name)
        p.send_signal(sig)
        try:
            rc = p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            rc = None
        check(rc == 0, "%s: exits 0 (got %r)" % (name, rc))

    # Without --autoplay the transport stays stopped.
    log = os.path.join(tmp, "noauto.log")
    with open(log, "w") as err:
        p = subprocess.Popen([BINARY, "--headless", SONG], env=env, stderr=err, stdin=subprocess.DEVNULL)
    check(wait_for(log, "Capture device"), "no autoplay: started")
    time.sleep(1)
    check("Playing" not in open(log).read(), "no autoplay: stays stopped")
    p.send_signal(signal.SIGTERM)
    p.wait(timeout=10)

    # Daemon: parent returns 0 once running, pid file and log file written.
    log, pidf = os.path.join(tmp, "d.log"), os.path.join(tmp, "d.pid")
    r = subprocess.run([BINARY, "--daemon", "--autoplay", "--log-file", log, "--pid-file", pidf, SONG], env=env, timeout=30)
    check(r.returncode == 0, "daemon: launcher exits 0")
    pid = int(open(pidf).read())
    check(wait_for(log, "Playing"), "daemon: log file shows Playing")
    os.kill(pid, signal.SIGTERM)
    check(pid_gone(pid), "daemon: exits on SIGTERM")

    # Daemon with a song that can't load: launcher reports failure.
    r = subprocess.run([BINARY, "--daemon", "--log-file", os.path.join(tmp, "bad.log"), os.path.join(tmp, "missing.xml")], env=env, timeout=30, stderr=subprocess.DEVNULL)
    check(r.returncode != 0, "daemon: launcher exits non-zero when the song fails to load")

    # Flag validation.
    r = subprocess.run([BINARY, "--autoplay", SONG], env=env, timeout=30, stderr=subprocess.DEVNULL)
    check(r.returncode != 0, "--autoplay without --headless is rejected")
    r = subprocess.run([BINARY, "--headless", "--log-file", os.path.join(tmp, "x.log"), SONG], env=env, timeout=30, stderr=subprocess.DEVNULL)
    check(r.returncode != 0, "--log-file without --daemon is rejected")

sys.exit(1 if failures else 0)
