#!/usr/bin/env python3
"""Runs the terminal frame pacer against shaped ptys (ptylink.py) and checks
what it does. Usage: run_assertions.py PROBE [--only N] [--report]

The program under test is cellui_pacing_probe. Each check prints its numbers,
so a run is also a report for tuning PacerConfig.
"""
import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptylink import Link  # noqa: E402

RESULTS = []
PROBE_ARGS = []  # --probe-arg: options added to every probe run


def parse(path):
    d = {"ticks": [], "keys": [], "phases": {}, "result": {}, "error": None}
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == "TICK":
            d["ticks"].append((int(w[1]) / 1e6, int(w[2].split("=")[1])))
            kv = dict(x.split("=") for x in w[3:] if "=" in x)
            d.setdefault("tick_bw", []).append((int(w[1]) / 1e6, float(kv.get("bandwidth", 0)), int(kv.get("class", 0))))
        elif w[0] == "KEY":
            d["keys"].append((int(w[1]) / 1e6, int(w[2])))
        elif w[0] == "PHASE":
            kv = dict(x.split("=") for x in w[2:])
            d["phases"][w[1]] = {k: float(v) for k, v in kv.items()}
            d["phases"][w[1]]["t"] /= 1e6
        elif w[0] == "RESULT":
            d["result"] = {k: float(v) for k, v in (x.split("=") for x in w[1:])}
        elif w[0] == "LINK":
            d["link"] = dict(x.split("=", 1) for x in w[1:] if "=" in x)
            d["link"]["description"] = line.split("description=", 1)[1].strip() if "description=" in line else ""
        elif w[0] == "ERROR":
            d["error"] = line
    return d


def run(probe, link, args, keys=(), env=None):
    out = tempfile.NamedTemporaryFile(prefix="pacing", suffix=".log", delete=False).name
    res = link.run([probe] + args + PROBE_ARGS + ["--out", out], env=env, keys=keys)
    log = parse(out)
    os.unlink(out)
    if log["error"] or res.timed_out or not log["result"]:
        raise RuntimeError("probe failed: %s %s" % (log["error"], res.exit_status))
    return res, log


def fps_between(log, start, t0, t1):
    pts = [(t - start, f) for t, f in log["ticks"]]
    a = [p for p in pts if p[0] >= t0]
    b = [p for p in pts if p[0] <= t1]
    if not a or not b or b[-1][0] <= a[0][0]:
        return 0.0
    return (b[-1][1] - a[0][1]) / (b[-1][0] - a[0][0])


def frame_size(log):
    r = log["result"]
    return (r["upload"] + r["placement"] + r["other"]) / max(1.0, r["frames"])


def check(name, ok, detail):
    RESULTS.append(ok)
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    sys.stdout.flush()


# 1. A long delay does not set the frame rate: fps follows B/S, not 1/RTT.
def t1(probe):
    bw = 520000
    res, log = run(probe, Link(bandwidth=bw, up_ms=400, down_ms=400), ["--mode", "churn", "--seconds", "14"])
    s = frame_size(log)
    want = bw / s
    got = fps_between(log, res.start, 5, 14)
    check("1 high latency", abs(got - want) <= 0.15 * want and got > 4 * (1 / 0.8),
          "fps %.1f, B/S %.1f (frame %.0f B, 1/RTT %.2f), estimated B %.0f of %d" % (got, want, s, 1 / 0.8, log["result"]["bandwidth"], bw))


# 2. A bandwidth cap lowers the frame rate and the estimate follows it.
def t2(probe):
    bw = 100000
    # The class is pinned: the picture quality follows it (check 9), and this check is about the pacing.
    res, log = run(probe, Link(bandwidth=bw, up_ms=10, down_ms=10), ["--mode", "churn", "--seconds", "12", "--pin", "full"])
    s = frame_size(log)
    want = bw / s
    got = fps_between(log, res.start, 4, 12)
    est = log["result"]["bandwidth"]
    _, fast = run(probe, Link(), ["--mode", "churn", "--seconds", "5"])
    fast_fps = fps_between(fast, 0, 0, 1e9) if False else fast["result"]["fps"]
    check("2 bandwidth cap", abs(got - want) <= 0.25 * want and abs(est - bw) <= 0.25 * bw and got < 0.2 * fast_fps,
          "fps %.1f, B/S %.1f, estimated B %.0f of %d, unshaped fps %.1f" % (got, want, est, bw, fast_fps))


# 3. Input stays responsive while frames are in flight on a slow link.
def t3(probe):
    keys = [(4.0, b"a"), (5.0, b"b"), (6.0, b"c"), (7.0, b"d")]
    res, log = run(probe, Link(bandwidth=100000, up_ms=10, down_ms=10), ["--mode", "churn", "--seconds", "9"], keys=keys)
    lat = [kt - st for (kt, _), st in zip(log["keys"], res.keys_sent)]
    ok = len(log["keys"]) == len(keys) and lat and max(lat) < 0.25
    check("3 input responsive", ok, "keys %d/%d, latency max %.0f ms (link delay 10 ms)" % (len(log["keys"]), len(keys), 1000 * max(lat or [9])))


def static(probe, protocol):
    res, log = run(probe, Link(), ["--mode", "static", "--seconds", "4.5", "--images", "6"], env={"CELLUI_PIXELS": protocol})
    ph = log["phases"]
    return res, log, ph


# 4. Unchanged pictures and regions are not sent again.
def t4(probe):
    n = 6
    res, log, ph = static(probe, "kitty")
    w1 = (ph["settled"]["t"], ph["after-text-change"]["t"])
    w2 = (ph["after-text-change"]["t"], ph["after-removal"]["t"])

    def window(w, action, margin=0.02):
        return sum(1 for (t, a, _) in res.kitty if a == action and w[0] + margin <= t <= w[1] - 0.02)

    text_only = window(w1, "t") + window(w1, "p") + window(w1, "d") == 0 and ph["after-text-change"]["upload"] == ph["settled"]["upload"] \
        and ph["after-text-change"]["placement"] == ph["settled"]["placement"]
    # The removal frame follows the phase mark at once, so its window has no margin at the start.
    removal = window(w2, "d", 0) == 1 and window(w2, "t", 0) == 0 and window(w2, "p", 0) == 0
    first = res.count("t") == n and res.count("p") == n
    check("4 kitty damage", text_only and removal and first,
          "uploads %d/%d, placements %d/%d; text change: %d pic cmds, removal window: %d delete, %d upload, %d place" % (
              res.count("t"), n, res.count("p"), n, window(w1, "t") + window(w1, "p") + window(w1, "d"), window(w2, "d", 0), window(w2, "t", 0), window(w2, "p", 0)))
    res, log, ph = static(probe, "sixel")
    later = [t for t in res.sixel if t - res.start > ph["settled"]["t"] - res.start + 0.1 + 0]
    after_settle = [t for t in res.sixel if t >= ph["settled"]["t"] + 0.02]
    check("4 sixel damage", len(res.sixel) == n and not after_settle,
          "sixel images %d (drawn once each: %d), sent after they settled: %d" % (len(res.sixel), n, len(after_settle)))


# 5. A reply that arrives in pieces is still matched; its text never reaches the application.
def t5(probe):
    a_res, a = run(probe, Link(split_reply=(3, 0.2, 4, 3)), ["--mode", "churn", "--seconds", "6", "--img", "16x16"])
    b_res, b = run(probe, Link(split_reply=(1, 0.2, 4, 3)), ["--mode", "churn", "--seconds", "6", "--img", "16x16"])
    ra, rb = a["result"], b["result"]
    stray_a = [k for _, k in a["keys"]]
    stray_b = [k for _, k in b["keys"] if k != 27]
    ok = not stray_a and not stray_b and ra["fences"] == 1 and rb["fences"] == 1 and ra["acks"] > 20 and rb["acks"] > 20 and \
        ra["stalls"] == 0 and rb["stalls"] == 0
    check("5 split reply", ok, "split after ESC [ ?: %d keys, acks %d; split after ESC: %d keys (%d Escape), acks %d; pieces %d+%d" % (
        len(a["keys"]), ra["acks"], len(b["keys"]), len(b["keys"]) - len(stray_b), rb["acks"], a_res.splits, b_res.splits))


# 6. A terminal that never answers gets about 4 fps.
def t6(probe):
    res, log = run(probe, Link(answer_dsr=False), ["--mode", "churn", "--seconds", "8"])
    r = log["result"]
    got = fps_between(log, res.start, 2, 8)
    check("6 no DSR", r["fences"] == 0 and 3 <= got <= 5, "fences %d, fps %.2f" % (r["fences"], got))


# 7. A stall halves the estimate and the rate comes back.
def t7(probe):
    bw = 300000
    res, log = run(probe, Link(bandwidth=bw, up_ms=50, down_ms=50, freeze=(6.0, 3.0)), ["--mode", "churn", "--seconds", "20"])
    before = fps_between(log, res.start, 3, 6)
    after = fps_between(log, res.start, 14, 20)
    r = log["result"]
    check("7 stall", r["stalls"] >= 1 and after >= 0.7 * before and r["bandwidth"] >= 0.5 * bw,
          "stalls %d, fps before %.1f after %.1f, estimated B %.0f of %d" % (r["stalls"], before, after, r["bandwidth"], bw))


# 8. The link's class follows the link: a link that holds frames back is classed by its bandwidth, a
# link that does not stays at full speed, a pinned class holds, and none of it flaps.
def t8(probe):
    cases = [  # (picture, link bandwidth, the class it is, in enum order: 0 full, 1 30, 2 15, 3 5, 4 1)
        ("328x80", 20e6, 0), ("328x80", 450e3, 1), ("232x80", 200e3, 2), ("80x80", 80e3, 3), ("16x16", 30e3, 4)]
    seen = []
    ok = True
    for img, bw, want in cases:
        # The slowest link is classed last (frames of a kilobyte, a class change needs seconds of a bound link).
        res, log = run(probe, Link(bandwidth=bw, up_ms=5, down_ms=5), ["--mode", "churn", "--seconds", "20" if bw < 50e3 else "13", "--img", img])
        link = log.get("link", {})
        got, changes = int(link.get("class", -1)), int(link.get("changes", 99))
        ok = ok and got == want and changes <= 1
        seen.append("%s: class %d (want %d), %d change%s, \"%s\"" % ("%.3g kB/s" % (bw / 1e3), got, want, changes, "" if changes == 1 else "s", link.get("description", "")))
    res, log = run(probe, Link(bandwidth=20e6, up_ms=5, down_ms=5), ["--mode", "churn", "--seconds", "8", "--img", "328x80", "--pin", "15"])
    link = log.get("link", {})
    pinned_ok = link.get("class") == "2" and link.get("pinned") == "1"
    # Frames of a kilobyte on a fast, long link: not the link's limit, and no rate shown that is only theirs.
    res, small = run(probe, Link(bandwidth=5e6, up_ms=20, down_ms=20), ["--mode", "churn", "--seconds", "13", "--img", "16x16"])
    small = small.get("link", {})
    small_ok = small.get("class") == "0" and small.get("description", "").startswith("not limiting")
    seen.append("16x16 at 5 MB/s, RTT 40 ms: class %s \"%s\"" % (small.get("class"), small.get("description", "")))
    check("8 link class", ok and pinned_ok and small_ok, "; ".join(seen) + "; pinned at 15 on a 20 MB/s link: class %s pinned %s" % (link.get("class"), link.get("pinned")))


# 9. A slow link turns the pictures' colours down: the same churn on a 100 kB/s link sends smaller frames
# than with the class held at full, and so more of them.
def t9(probe):
    bw = 100000
    link = lambda: Link(bandwidth=bw, up_ms=10, down_ms=10)
    _, held = run(probe, link(), ["--mode", "churn", "--seconds", "14", "--pin", "full"])
    _, free = run(probe, link(), ["--mode", "churn", "--seconds", "14"])
    held_size, free_size = frame_size(held), frame_size(free)
    held_fps, free_fps = held["result"]["fps"], free["result"]["fps"]
    check("9 picture quality", free_size < 0.8 * held_size and free_fps > 1.2 * held_fps,
          "frame %.0f B held at full, %.0f B following the class; %.1f fps and %.1f fps" % (held_size, free_size, held_fps, free_fps))


# 10. A terminal that answers late while it is busy is not a slow link. 5 MB/s, replies 5 ms late when idle and 40 ms
# when busy (what it took over the last 100 ms), a 3 s burst of 25 kB frames and then 10 s of frames of a
# kilobyte or two (or 5 kB, `--variant 5k`): the estimate and the class must hold through the small frames.
def burst_check(probe, name, small_bytes):
    link = Link(bandwidth=5e6, up_ms=1, down_ms=1, reply_delay_load=(5, 40, 4096))
    res, log = run(probe, link, ["--mode", "burst", "--burst-seconds", "3", "--small-bytes", str(small_bytes), "--seconds", "14"])
    ticks = [(t - res.start, b, c) for t, b, c in log["tick_bw"]]
    burst = [x for x in ticks if x[0] <= 3.2 and x[0] >= 2.0]
    small = [x for x in ticks if x[0] > 4.0]
    if not burst or not small:
        check(name, False, "no ticks in the phases (%d, %d)" % (len(burst), len(small)))
        return
    b0 = burst[-1][1]
    low = min(b for _, b, _ in small)
    classes = sorted(set(c for _, _, c in ticks))
    check(name, low >= 0.5 * b0 and classes == [0],
          "B %.0f kB/s at the end of the burst, lowest %.0f kB/s in the small-frame phase (%.2fx), classes seen %s" % (b0 / 1e3, low / 1e3, low / b0, classes))


def t10(probe):
    burst_check(probe, "10 busy terminal, small frames", 1500)


def t11(probe):
    burst_check(probe, "11 busy terminal, 5 kB frames", 5000)


TESTS = [t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("probe")
    ap.add_argument("--only", type=int, action="append")
    ap.add_argument("--probe-arg", action="append", default=[], help="an option for every probe run, as --probe-arg=--min-evidence --probe-arg=0")
    a = ap.parse_args()
    PROBE_ARGS.extend(a.probe_arg)
    for i, t in enumerate(TESTS, 1):
        if a.only and i not in a.only:
            continue
        try:
            t(a.probe)
        except Exception as e:  # noqa: BLE001
            check("%d" % i, False, "error %r" % (e,))
    print("%d/%d passed" % (sum(RESULTS), len(RESULTS)))
    return 0 if all(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
