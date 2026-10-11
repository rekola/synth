#!/usr/bin/env python3
"""The modern-look colour tokens (plans/modern-look-tokens.md): OKLCH values
for the dark and light themes, converted to sRGB hex and checked against the
WCAG contrast rules. Prints a report; with an output path, also writes the
hex values as JSON.

    python3 tools/modern_look/tokens.py [tokens.json]"""
import math, json, sys

def oklch_to_srgb(L, C, h):
    a = C*math.cos(math.radians(h)); b = C*math.sin(math.radians(h))
    l_ = L + 0.3963377774*a + 0.2158037573*b
    m_ = L - 0.1055613458*a - 0.0638541728*b
    s_ = L - 0.0894841775*a - 1.2914855480*b
    l, m, s = l_**3, m_**3, s_**3
    r = 4.0767416621*l - 3.3077115913*m + 0.2309699292*s
    g = -1.2684380046*l + 2.6097574011*m - 0.3413193965*s
    bl = -0.0041960863*l - 0.7034186147*m + 1.7076147010*s
    def gam(x):
        x = max(0.0, min(1.0, x))
        return 12.92*x if x <= 0.0031308 else 1.055*x**(1/2.4)-0.055
    return tuple(gam(v) for v in (r, g, bl))

def hexof(rgb): return '#%02x%02x%02x' % tuple(round(v*255) for v in rgb)

def lum(rgb):
    def lin(c): return c/12.92 if c <= 0.03928 else ((c+0.055)/1.055)**2.4
    r, g, b = (lin(v) for v in rgb)
    return 0.2126*r + 0.7152*g + 0.0722*b

def contrast(a, b):
    la, lb = lum(a), lum(b)
    if la < lb: la, lb = lb, la
    return (la+0.05)/(lb+0.05)

H = 250  # slightly cool neutrals

# Theme-dependent: surfaces, text, the accent.
THEMES = {
 'dark': {
  'surface.base':   (0.175, 0.010, H),
  'surface.panel':  (0.215, 0.011, H),
  'surface.raised': (0.265, 0.012, H),
  'surface.overlay':(0.315, 0.013, H),
  'text.primary':   (0.940, 0.008, H),
  'text.secondary': (0.760, 0.010, H),
  'text.disabled':  (0.560, 0.010, H),
  'accent':         (0.800, 0.085, 215),
  'accent.dim':     (0.380, 0.045, 215),
  'text.on-accent': (0.180, 0.020, 215),
 },
 'light': {
  'surface.base':   (0.975, 0.005, H),
  'surface.panel':  (0.945, 0.006, H),
  'surface.raised': (0.915, 0.007, H),
  'surface.overlay':(0.985, 0.004, H),
  'text.primary':   (0.230, 0.015, H),
  'text.secondary': (0.430, 0.015, H),
  'text.disabled':  (0.560, 0.012, H),
  'accent':         (0.480, 0.110, 225),
  'accent.dim':     (0.880, 0.040, 225),
  'text.on-accent': (0.985, 0.005, 225),
 },
}

# Shared with the Launchpad, so the same in every theme: the Launchpad is
# always dark and has no icons, so a clip's colour (its track's) and its
# state must read the same on the pads and on the screen. One lightness band,
# L 0.50-0.64, is readable (3:1) on both the darkest and the lightest surface.
# playing also means queued to play, and recording also queued to record: the
# pads tell them apart by pulsing and flashing, the terminal by the glyph.
SHARED = {
 'state.playing':        (0.600, 0.190, 145),
 'state.playing.dim':    (0.580, 0.060, 145),   # paused
 'state.recording':      (0.600, 0.210, 27),
 'state.recording.dim':  (0.580, 0.075, 27),    # armed, queued to stop
 'text.on-track':        (0.130, 0.015, H),     # text on a track or state colour
 'data.heat':            (0.720, 0.140, 65),    # the DirAC heatmap's fixed hue
}
# The level meter ramp, quiet to loud (not a Launchpad matter).
METER = [(0.0, (0.560, 0.130, 150)), (0.5, (0.700, 0.185, 140)), (0.78, (0.780, 0.160, 85)), (0.92, (0.680, 0.200, 45)), (1.0, (0.600, 0.215, 27))]

# Track identity colours: one lightness and chroma, hues spread by the
# golden ratio over the arcs the Launchpad's own state colours leave free
# (not near its green or red).
TRACK_L, TRACK_C = 0.60, 0.12
ARCS = [(45.0, 105.0), (180.0, 340.0)]
def track_hue(i):
    t = (0.5 + i * 0.6180339887) % 1.0
    pos = t * sum(b - a for a, b in ARCS)
    for a, b in ARCS:
        if pos <= b - a: return a + pos
        pos -= b - a
    return ARCS[-1][1]

out = {}
def put(name, d): out.setdefault(name, {}).update(d)
shared_rgb = {k: oklch_to_srgb(*v) for k, v in SHARED.items()}
tracks_rgb = [oklch_to_srgb(TRACK_L, TRACK_C, track_hue(i)) for i in range(8)]
meter_rgb = [(f, oklch_to_srgb(*c)) for f, c in METER]
for name, th in THEMES.items():
    rgb = {k: oklch_to_srgb(*v) for k, v in th.items()}
    put(name, {k: hexof(v) for k, v in rgb.items()})
    base, panel, raised = rgb['surface.base'], rgb['surface.panel'], rgb['surface.raised']
    print('==', name)
    checks = [
      ('text.primary / base', rgb['text.primary'], base, 4.5),
      ('text.primary / raised', rgb['text.primary'], raised, 4.5),
      ('text.secondary / base', rgb['text.secondary'], base, 4.5),
      ('text.secondary / panel', rgb['text.secondary'], panel, 4.5),
      ('text.disabled / base (UI glyph)', rgb['text.disabled'], base, 3.0),
      ('accent / base (UI glyph)', rgb['accent'], base, 3.0),
      ('text.on-accent / accent', rgb['text.on-accent'], rgb['accent'], 4.5),
      ('text.primary / accent.dim', rgb['text.primary'], rgb['accent.dim'], 4.5),
    ]
    for label, fg, bg, need in checks:
        r = contrast(fg, bg)
        print(f'{label:34s} {r:5.2f}  need {need}  {"ok" if r >= need else "FAIL"}')
    # shared colours against this theme's surfaces
    for k, v in shared_rgb.items():
        if k in ('text.on-track', 'data.heat'): continue
        for sname, srgb in (('base', base), ('panel', panel)):
            r = contrast(v, srgb)
            print(f'{k + " / " + sname:34s} {r:5.2f}  need 3.0  {"ok" if r >= 3.0 else "FAIL"}')
    for i, v in enumerate(tracks_rgb):
        for sname, srgb in (('base', base), ('panel', panel)):
            r = contrast(v, srgb)
            print(f'{"track " + str(i) + " / " + sname:34s} {r:5.2f}  need 3.0  {"ok" if r >= 3.0 else "FAIL"}')
print('== shared (text on the colour)')
for k in [k for k in SHARED if k not in ('text.on-track', 'data.heat')] + [f'track {i}' for i in range(8)]:
    v = shared_rgb[k] if k in shared_rgb else tracks_rgb[int(k.split()[1])]
    r = contrast(shared_rgb['text.on-track'], v)
    print(f'{"text.on-track / " + k:34s} {r:5.2f}  need 4.5  {"ok" if r >= 4.5 else "FAIL"}')
print('== track hues, distance to the state hues (need >= 35 degrees)')
for i in range(8):
    h = track_hue(i)
    d = min(abs((h - sh + 180) % 360 - 180) for sh in (145, 27))
    print(f'track {i}: hue {h:6.1f}  nearest state hue {d:5.1f}  {"ok" if d >= 35 else "FAIL"}')
sh = {k: hexof(v) for k, v in shared_rgb.items()}
sh['tracks'] = [hexof(v) for v in tracks_rgb]
sh['track_hues'] = [round(track_hue(i), 1) for i in range(8)]
sh['meter'] = [[f, hexof(c)] for f, c in meter_rgb]
out['shared'] = sh
if len(sys.argv) > 1:
    json.dump(out, open(sys.argv[1], 'w'), indent=1)
