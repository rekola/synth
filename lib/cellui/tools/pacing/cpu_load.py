#!/usr/bin/env python3
"""What the UI costs the audio (phase 8 of the link classes): the application through the emulated
terminal in each link class, picture protocol and scenario, optionally with its core shared with a busy
loop (a loaded or slow machine), and per run

  - the UI thread's CPU (a core's share), and each stage's share of it: picture render, backdrop, cell
    encode, image encode and what it holds (deflate, base64, PNG, Sixel palette, map and bytes),
    from the application's `SYNTH_LINK_STATS` log;
  - the audio thread's blocks, CPU per block against the block's length, worst block, late blocks, the
    longest gap between blocks and xruns, from `SYNTH_AUDIO_STATS`.

    cpu_load.py SYNTH [--classes full,30,15,5,1] [--protocols kitty,sixel] [--scenarios live,arrangement,dialog]
                      [--hogs N] [--seconds 20] [--song songs/demo3.xml]

Nested stages are inclusive: image_encode holds the stages below it, so they are not to be summed.
Not a ctest: it needs the application and takes minutes.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptylink import Link  # noqa: E402

KEYS = {
    "live": [(3.0, b" ")],
    "arrangement": [(3.0, b" "), (4.0, b"\t")],
    "dialog": [(3.0, b" "), (5.0, b"\x1bx"), (5.6, b"about"), (6.2, b"\r")],
}
WARMUP = 8  # seconds of the log skipped: start-up, the first pictures
STAGES = ["picture_render", "backdrop", "cell_encode", "image_encode", "deflate", "base64", "png", "sixel_palette", "sixel_map", "sixel_encode"]


def parse_fields(line):
    out = {}
    for part in line.split():
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v
    return out


def link_lines(path):
    rows = []
    for line in open(path):
        f = parse_fields(line)
        stages = {}
        if f.get("stages", "-") != "-":
            for item in f["stages"].split(","):
                name, us = item.split(":")
                stages[name] = int(us)
        rows.append({"t": float(f["t"]), "cpu_us": int(f.get("cpu_us", 0)), "stages": stages, "bytes": int(f["upload"]) + int(f["placement"]) + int(f["other"])})
    return rows[WARMUP:]


def audio_lines(path):
    rows = []
    for line in open(path):
        f = parse_fields(line)
        rows.append({k: float(v) for k, v in f.items()})
    return rows[WARMUP:]


def run_one(synth, song, cls, protocol, scenario, hogs, seconds):
    d = tempfile.mkdtemp()
    link_log, audio_log = os.path.join(d, "link.txt"), os.path.join(d, "audio.txt")
    env = {"CELLUI_PIXELS": protocol, "SYNTH_LINK_STATS": link_log, "SYNTH_AUDIO_STATS": audio_log}
    cmd = [synth, "--null-audio", "--ui", "terminal", "--link-class", cls, song]
    hog = []
    if hogs:
        taskset = shutil.which("taskset")
        if not taskset:
            raise SystemExit("--loaded needs taskset")
        cmd = [taskset, "-c", "0"] + cmd  # the link runs argv[0] without a path search
        hog = [subprocess.Popen([taskset, "-c", "0", sys.executable, "-c", "while True: pass"]) for _ in range(hogs)]
    try:
        Link().run(cmd, env=env, keys=KEYS[scenario], duration=seconds)
    finally:
        for h in hog:
            h.kill()
            h.wait()
    if not os.path.exists(link_log) or not os.path.exists(audio_log):
        return None
    return link_lines(link_log), audio_lines(audio_log)


def summarise(link, audio):
    n = max(1, len(link))
    ui = sum(r["cpu_us"] for r in link) / n / 1e4  # microseconds a second to percent of a core
    stage = {s: sum(r["stages"].get(s, 0) for r in link) / n / 1e4 for s in STAGES}
    kbs = sum(r["bytes"] for r in link) / n / 1000
    m = max(1, len(audio))
    blocks = sum(r["blocks"] for r in audio)
    period = audio[0]["period_ms"] if audio else 0
    render = sum(r["render_cpu_us"] for r in audio)
    budget = blocks * period * 1000 if blocks else 1
    return {
        "ui": ui, "stage": stage, "kbs": kbs,
        "blocks": blocks / m, "render_share": 100.0 * render / budget, "max_render_ms": max((r["max_render_us"] for r in audio), default=0) / 1000,
        "late": sum(r["late"] for r in audio), "max_gap": max((r["max_gap_ms"] for r in audio), default=0), "period": period,
        "xruns": sum(r["xruns"] for r in audio),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("synth")
    ap.add_argument("--song", default="songs/demo3.xml")
    ap.add_argument("--classes", default="full,30,15,5,1")
    ap.add_argument("--protocols", default="kitty,sixel")
    ap.add_argument("--scenarios", default="live,arrangement,dialog")
    ap.add_argument("--hogs", type=int, default=0, help="the application on one core shared with this many busy loops")
    ap.add_argument("--seconds", type=int, default=20)
    a = ap.parse_args()
    synth = os.path.abspath(a.synth)
    print("%-6s %-6s %-12s | %5s %6s | %s | audio: %5s %6s %6s %5s %7s %5s" % (
        "class", "proto", "scenario", "UI%", "kB/s", " ".join("%s" % s[:9].rjust(9) for s in STAGES[:5]) + " ...", "blk/s", "cpu%", "maxms", "late", "gapms", "xrun"))
    for proto in a.protocols.split(","):
        for scenario in a.scenarios.split(","):
            for cls in a.classes.split(","):
                got = run_one(synth, a.song, cls, proto, scenario, a.hogs, a.seconds)
                if not got:
                    print("%-6s %-6s %-12s | no log" % (cls, proto, scenario))
                    continue
                s = summarise(*got)
                st = s["stage"]
                print("%-6s %-6s %-12s | %5.1f %6.0f | %s | %5.0f %6.2f %6.2f %5d %7.1f %5d" % (
                    cls, proto, scenario, s["ui"], s["kbs"],
                    " ".join("%9.2f" % st[k] for k in STAGES[:5]) + "  " + " ".join("%s=%.2f" % (k, st[k]) for k in STAGES[5:] if st[k] >= 0.005),
                    s["blocks"], s["render_share"], s["max_render_ms"], s["late"], s["max_gap"], s["xruns"]))
                sys.stdout.flush()
    print("\nstage columns (percent of a core): %s" % ", ".join(STAGES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
