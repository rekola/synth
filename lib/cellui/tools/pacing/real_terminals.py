#!/usr/bin/env python3
"""The application in real terminal emulators, on a virtual display (xvfb-run): play ten seconds, stop, and
let the scopes decay for thirty, with the application's --pacer-log (one line per frame the terminal took in:
time, bytes, response delay D, estimate B, class, bytes in flight, grade, sample) written to a file per
terminal. Prints B, D and the class over the decay.

    real_terminals.py SYNTH {vte,gnome-terminal,kitty,xterm}... [--outdir DIR] [--song songs/demo3.xml]

vte: a Vte.Terminal widget in a window (vte_host.py, python3.12 with python3-gi and gir1.2-vte-2.91).
gnome-terminal: the real program, under dbus-run-session (a UTF-8 locale, and python3.12 where the system's
python3 is another version than python3-gi was built for); keys are typed with xdotool.
kitty: software GL (LIBGL_ALWAYS_SOFTWARE=1); keys by its remote control. xterm: -ti vt340, so Sixel is on.
"""
import argparse
import os
import shlex
import subprocess
import sys
import tempfile
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
PLAY, STOP, END = 6, 16, 50  # seconds after the start: play, stop, end
HOG = False  # --hog: busy loops on every core between play and stop


def app(synth, log, song):
    return [synth, "--null-audio", "--ui", "terminal", "--pacer-log", log, song]


def script_for(kind, synth, log, song, workdir):
    q = lambda l: " ".join(shlex.quote(x) for x in l)
    a = q(app(synth, log, song))
    keys = f"""
sleep {PLAY}; type_space
sleep {STOP - PLAY}; type_space
sleep {END - STOP}
"""
    hog = ""
    if HOG:
        # CPU hogs while it plays: a busy desktop paints late, and so answers late.
        hog = f"( sleep {PLAY}; for i in $(seq {os.cpu_count()}); do yes >/dev/null & done; sleep {STOP - PLAY}; pkill yes ) &\n"
    if kind == "vte":
        return hog + f"""{HERE}/vte_host.py {END} {PLAY}:'\\x20' {STOP}:'\\x20' -- {a}"""
    if kind == "xterm":
        return textwrap.dedent(f"""
            xterm -ti vt340 -geometry 120x40 -xrm 'XTerm*allowSendEvents: true' -e {a} &
            XPID=$!
            sleep 2
            WID=$(xdotool search --onlyvisible --class xterm | head -1)
            type_space() {{ xdotool type --window "$WID" ' '; }}
            {keys}
            kill $XPID 2>/dev/null; wait $XPID 2>/dev/null; true
        """)
    if kind == "kitty":
        sock = os.path.join(workdir, "kitty.sock")
        return textwrap.dedent(f"""
            export LIBGL_ALWAYS_SOFTWARE=1
            kitty -o allow_remote_control=yes --listen-on unix:{sock} -o initial_window_width=120c -o initial_window_height=40c \\
                  -o confirm_os_window_close=0 {a} &
            KPID=$!
            sleep 4
            type_space() {{ kitty @ --to unix:{sock} send-text ' '; }}
            {keys}
            kill $KPID 2>/dev/null; wait $KPID 2>/dev/null; true
        """)
    if kind == "gnome-terminal":
        return textwrap.dedent(f"""
            dbus-run-session -- bash -c '
              export LANG=C.UTF-8 LC_ALL=C.UTF-8
              python3.12 /usr/bin/gnome-terminal --wait --geometry=120x40 -- {a} &
              GPID=$!
              sleep 4
              WID=$(xdotool search --onlyvisible --class gnome-terminal | head -1)
              xdotool windowfocus "$WID" 2>/dev/null
              type_space() {{ xdotool key --window "$WID" space; }}
              {keys}
              pkill -f gnome-terminal-server; kill $GPID 2>/dev/null; true
            '
        """)
    raise SystemExit("unknown terminal " + kind)


def parse(path):
    rows = []
    for line in open(path):
        kv = dict(x.split("=", 1) for x in line.split())
        rows.append({k: float(v.rstrip("ms")) for k, v in kv.items()})
    return rows


def report(kind, path):
    rows = parse(path)
    if not rows:
        print("%s: no frames logged" % kind)
        return
    # The session starts when the first frame is taken in; keys were typed at PLAY and STOP seconds after the start.
    t0 = rows[0]["t"]
    print("%s: %d frames; the log is %s" % (kind, len(rows), path))
    print("  %5s %7s %9s %9s %5s  (per 3 s: the last frame's D, B, class, mean bytes a frame)" % ("t", "frames", "D ms", "B kB/s", "class"))
    for lo in range(0, int(rows[-1]["t"] - t0) + 1, 3):
        chunk = [r for r in rows if lo <= r["t"] - t0 < lo + 3]
        if not chunk:
            continue
        last = chunk[-1]
        print("  %5d %7d %9.1f %9.0f %5d  S=%.0f" % (lo, len(chunk), last["D"], last["B"] / 1e3, last["class"], sum(r["S"] for r in chunk) / len(chunk)))
    classes = sorted(set(int(r["class"]) for r in rows))
    print("  classes seen: %s; B min %.0f max %.0f kB/s; D min %.1f max %.1f ms" % (classes, min(r["B"] for r in rows) / 1e3, max(r["B"] for r in rows) / 1e3, min(r["D"] for r in rows), max(r["D"] for r in rows)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("synth")
    ap.add_argument("terminals", nargs="+")
    ap.add_argument("--outdir", default=tempfile.gettempdir())
    ap.add_argument("--song", default="songs/demo3.xml")
    ap.add_argument("--hog", action="store_true", help="CPU hogs on every core between play and stop")
    a = ap.parse_args()
    global HOG
    HOG = a.hog
    os.makedirs(a.outdir, exist_ok=True)
    for kind in a.terminals:
        log = os.path.join(a.outdir, "pacer-%s.log" % kind)
        if os.path.exists(log):
            os.unlink(log)
        body = script_for(kind, os.path.abspath(a.synth), log, a.song, a.outdir)
        sh = os.path.join(a.outdir, "run-%s.sh" % kind)
        open(sh, "w").write("#!/bin/bash\n" + body + "\n")
        os.chmod(sh, 0o755)
        subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1280x900x24", "bash", sh], timeout=END + 90)
        if os.path.exists(log):
            report(kind, log)
        else:
            print("%s: no log was written" % kind)
    return 0


if __name__ == "__main__":
    sys.exit(main())
