"""A pty between the program under test and an emulated terminal, with the link
in between shaped: bandwidth, latency in each direction, the terminal's decode
speed. The terminal answers the start-up queries, the cursor position queries
the frames are fenced with, and counts the kitty graphics commands and Sixel
images it is sent. Standard library only.
"""
import base64
import errno
import fcntl
import heapq
import os
import pty
import re
import select
import struct
import termios
import time

ROWS, COLS, CELL_H, CELL_W = 40, 120, 16, 8


class Link:
    def __init__(self, bandwidth=None, up_ms=0.0, down_ms=0.0, decode=None, answer_dsr=True,
                 split_reply=None, freeze=None):
        self.bandwidth = bandwidth          # bytes/s from the program to the terminal; None: unshaped
        self.up = up_ms / 1000.0            # program -> terminal
        self.down = down_ms / 1000.0        # terminal -> program (replies, keys)
        self.decode = decode                # bytes/s the terminal parses; None: instant
        self.answer_dsr = answer_dsr
        self.split_reply = split_reply      # (bytes in the first piece, delay s, how many replies, how many to let by first) for fence replies
        self.freeze = freeze                # (start s, duration s): the terminal neither reads nor answers

    # -- running ----------------------------------------------------------
    def run(self, argv, env=None, keys=(), duration=None, keep_pictures=False):
        """Runs argv on the program's side of the link. `keys` are (time s, bytes)
        typed at the terminal; `duration` ends a program that does not end itself
        (SIGTERM). Returns a Result."""
        res = Result()
        res.keep_pictures = keep_pictures
        environ = dict(os.environ)
        environ.update({"TERM": "xterm-256color", "COLORTERM": "truecolor", "CELLUI_PIXELS": "kitty"})
        environ.update(env or {})
        pid, master = pty.fork()
        if pid == 0:
            os.environ.clear()
            os.environ.update(environ)
            os.execv(argv[0], argv)
            os._exit(127)
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, COLS * CELL_W, ROWS * CELL_H))
        os.set_blocking(master, False)
        start = time.time()
        res.start = start
        replies = []                        # heap of (time, seq, bytes)
        seq = 0
        tokens = 0.0
        last = time.monotonic()
        proc_free = 0.0                     # when the terminal has parsed everything so far
        term = _Terminal(res)
        pending_keys = sorted(keys)
        block_until = 0.0
        splits_left = self.split_reply[2] if self.split_reply else 0
        skip_fences = self.split_reply[3] if self.split_reply else 0
        exited = False
        deadline_after_exit = None
        while True:
            now_m = time.monotonic()
            t = time.time() - start
            frozen = self.freeze and self.freeze[0] <= t < self.freeze[0] + self.freeze[1]
            # Keys typed at the terminal reach the program after the downstream delay.
            while pending_keys and pending_keys[0][0] <= t:
                kt, kb = pending_keys.pop(0)
                seq += 1
                heapq.heappush(replies, (time.time() + self.down, seq, kb, "key"))
                res.keys_sent.append(time.time())
            # Replies due. They go out in order, like the bytes of one stream: while
            # the rest of a split reply is awaited nothing else follows it.
            while replies and replies[0][0] <= time.time():
                kind = replies[0][3]
                if kind == "reply" and (frozen or time.time() < block_until):
                    # Held back: later, behind the continuation of a split reply or the freeze.
                    _, hseq, hdata, hkind = heapq.heappop(replies)
                    resume = self.freeze[0] + self.freeze[1] + start if frozen else block_until
                    heapq.heappush(replies, (max(resume, block_until) + 0.001, hseq, hdata, hkind))
                    continue
                _, _, data, kind = heapq.heappop(replies)
                if kind == "reply" and skip_fences > 0:
                    skip_fences -= 1
                    self._write(master, data)
                elif kind == "reply" and self.split_reply and splits_left > 0 and len(data) > self.split_reply[0]:
                    n = self.split_reply[0]
                    self._write(master, data[:n])
                    seq += 1
                    block_until = time.time() + self.split_reply[1] + 0.05
                    heapq.heappush(replies, (time.time() + self.split_reply[1], seq, data[n:], "cont"))
                    splits_left -= 1
                    res.splits += 1
                else:
                    self._write(master, data)
            # Read what the link allows.
            if self.bandwidth:
                tokens = min(tokens + (now_m - last) * self.bandwidth, max(1024.0, self.bandwidth * 0.01))
            last = now_m
            want = 65536 if not self.bandwidth else int(tokens)
            if frozen:
                want = 0
            data = b""
            if want > 0:
                try:
                    data = os.read(master, min(65536, want))
                except BlockingIOError:
                    data = b""
                except OSError as e:
                    if e.errno == errno.EIO:
                        exited = True
                    else:
                        raise
                if self.bandwidth:
                    tokens -= len(data)
            if data:
                arrive = time.time() + self.up
                # The terminal parses the chunk at its own speed; its replies leave when it is done.
                proc_free = max(proc_free, arrive) + (len(data) / self.decode if self.decode else 0.0)
                for kind, _length, payload in term.feed(data, arrive):
                    if kind == "reply" and not self.answer_dsr:
                        continue
                    if kind in ("reply", "plain"):
                        seq += 1
                        heapq.heappush(replies, (proc_free + self.down, seq, payload[0], "reply" if kind == "reply" else "key"))
            res.bytes_read += len(data)
            if data:
                res.series.append((time.time(), len(data)))
            # Wait for the next thing to do.
            timeout = 0.001 if (self.bandwidth and not data) else 0.002
            if not data and not self.bandwidth:
                timeout = 0.005
            select.select([master], [], [], timeout if not data else 0)
            wpid, status = os.waitpid(pid, os.WNOHANG)
            if wpid:
                exited = True
                res.exit_status = status
            if exited:
                if deadline_after_exit is None:
                    deadline_after_exit = time.time() + 0.1
                    # drain
                    try:
                        while True:
                            d = os.read(master, 65536)
                            if not d:
                                break
                            res.bytes_read += len(d)
                            list(term.feed(d, time.time()))
                    except OSError:
                        pass
                break
            if duration and t > duration and not res.terminated:
                os.kill(pid, 15)
                res.terminated = True
            if t > 120:
                os.kill(pid, 9)
                os.waitpid(pid, 0)
                res.timed_out = True
                break
        os.close(master)
        return res

    @staticmethod
    def _write(fd, data):
        try:
            os.write(fd, data)
        except (BlockingIOError, OSError):
            pass


class Result:
    def __init__(self):
        self.start = 0.0
        self.bytes_read = 0
        self.exit_status = None
        self.timed_out = False
        self.terminated = False
        self.splits = 0
        self.keys_sent = []
        self.kitty = []                      # (time, action, payload bytes)
        self.sixel = []                      # time of each Sixel image
        self.fences = []                     # time of each fence query
        self.chunks = 0
        self.keep_pictures = False
        self.series = []                     # (time, bytes) of each read from the program
        self.dims = []                       # (time, width, height) of each kitty upload
        self.pictures = []                   # (time, width, height, RGB bytes) of each kitty upload, when kept

    def count(self, action, t0=0.0, t1=1e9):
        return sum(1 for (t, a, n) in self.kitty if a == action and t0 <= t - self.start < t1)


_CSI = re.compile(rb"\x1b\[([0-9;?<>=!]*)([ -/]*)([@-~])")


class _Terminal:
    """Parses what the program wrote, finds the queries to answer and the
    graphics commands to count. A sequence is reported with its length, so the
    terminal's decode time can be charged for it."""

    def __init__(self, res):
        self.res = res
        self.buf = b""
        self.row, self.col = 1, 1

    def feed(self, data, t):
        self.buf += data
        out = []
        i = 0
        buf = self.buf
        n = len(buf)
        plain_start = 0
        while i < n:
            if buf[i] != 0x1B:
                i += 1
                continue
            if i + 1 >= n:
                break
            c = buf[i + 1]
            if c == ord("["):
                m = _CSI.match(buf, i)
                if not m:
                    if n - i > 64:
                        i += 2
                        continue
                    break
                params, inter, final = m.group(1), m.group(2), chr(m.group(3)[0])
                length = m.end() - i
                reply = self._csi(params.decode(), inter.decode(), final)
                if reply is not None:
                    is_fence = final == "n"
                    if is_fence:
                        self.res.fences.append(t)
                    out.append(("reply" if is_fence else "plain", length, (reply, is_fence)))
                else:
                    out.append(("skip", length, None))
                i = m.end()
            elif c in (ord("_"), ord("P"), ord("]")):
                end = buf.find(b"\x1b\\", i + 2)
                bel = buf.find(b"\x07", i + 2) if c == ord("]") else -1
                if end < 0 and bel < 0:
                    break
                stop = end + 2 if end >= 0 and (bel < 0 or end < bel) else bel + 1
                body = buf[i + 2:stop - (2 if stop == end + 2 and end >= 0 and (bel < 0 or end < bel) else 1)]
                reply = self._string(chr(c), body, t)
                out.append(("plain" if reply else "skip", stop - i, (reply,) if reply else None))
                i = stop
            else:
                i += 2
        self.buf = buf[i:] if i <= n else b""
        return [(k, ln, p) for (k, ln, p) in out]

    def _csi(self, params, inter, final):
        if final == "H":
            parts = params.split(";")
            self.row = int(parts[0] or 1)
            self.col = int(parts[1] or 1) if len(parts) > 1 else 1
            return None
        if final == "n" and params == "6":
            return f"\x1b[{self.row};{self.col}R".encode()
        if final == "n" and params == "?6":
            return f"\x1b[?{self.row};{self.col}R".encode()
        if final == "c" and params == "":
            return b"\x1b[?62c"
        if final == "t" and params == "16":
            return f"\x1b[6;{CELL_H};{CELL_W}t".encode()
        if final == "t" and params == "14":
            return f"\x1b[4;{ROWS * CELL_H};{COLS * CELL_W}t".encode()
        if final == "t" and params == "18":
            return f"\x1b[8;{ROWS};{COLS}t".encode()
        if final == "y" and inter == "$" and params == "?2026":
            return b"\x1b[?2026;2$y"
        return None

    def _collect(self, action, kv, payload, t):
        """Reassembles chunked kitty uploads (raw RGB or RGBA) into pictures."""
        if action == "t":
            self.cur = [t, int(kv.get(b"s", b"0")), int(kv.get(b"v", b"0")), int(kv.get(b"f", b"24")), bytearray(payload)]
        elif action == "" and getattr(self, "cur", None) is not None:
            self.cur[4].extend(payload)
        else:
            return
        if kv.get(b"m", b"0") == b"0" and self.cur is not None:
            t0, w, h, f, data = self.cur
            self.cur = None
            raw = base64.b64decode(bytes(data))
            if f == 32:
                raw = b"".join(raw[i:i + 3] for i in range(0, len(raw), 4))
            self.res.pictures.append((t0, w, h, raw))

    def _string(self, kind, body, t):
        if kind == "_" and body.startswith(b"G"):
            keys, _, payload = body[1:].partition(b";")
            kv = dict(p.split(b"=", 1) for p in keys.split(b",") if b"=" in p)
            action = kv.get(b"a", b"").decode()
            if self.res.keep_pictures:
                self._collect(action, kv, payload, t)
            if action == "q":
                return b"\x1b_Gi=31;OK\x1b\\"
            if action:
                self.res.kitty.append((t, action, len(body)))
                if action == "t":
                    self.res.dims.append((t, int(kv.get(b"s", b"0")), int(kv.get(b"v", b"0"))))
            else:
                self.res.kitty.append((t, "chunk", len(body)))
            return None
        if kind == "P" and re.match(rb"[0-9;]*q", body):
            self.res.sixel.append(t)
        return None
