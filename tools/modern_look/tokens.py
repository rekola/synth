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
  'state.playing':  (0.800, 0.170, 150),
  'state.playing.dim': (0.500, 0.100, 150),
  'state.recording':(0.680, 0.210, 25),
  'state.recording.dim': (0.540, 0.130, 25),
  'state.queued':   (0.820, 0.150, 95),
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
  'state.playing':  (0.520, 0.150, 150),
  'state.playing.dim': (0.620, 0.110, 150),
  'state.recording':(0.540, 0.210, 25),
  'state.recording.dim': (0.640, 0.120, 25),
  'state.queued':   (0.560, 0.130, 75),
 },
}

# Track identity colours: fixed lightness and chroma, hue by golden angle.
def track_hue(i): return (i*137.508 + 20) % 360
TRACK = {'dark': (0.74, 0.095), 'light': (0.55, 0.115)}

out = {}
for name, th in THEMES.items():
    rgb = {k: oklch_to_srgb(*v) for k, v in th.items()}
    out[name] = {k: hexof(v) for k, v in rgb.items()}
    L, C = TRACK[name]
    out[name]['tracks'] = [hexof(oklch_to_srgb(L, C, track_hue(i))) for i in range(8)]
    print('==', name)
    base, panel, raised = rgb['surface.base'], rgb['surface.panel'], rgb['surface.raised']
    checks = [
      ('text.primary / base', rgb['text.primary'], base, 4.5),
      ('text.primary / raised', rgb['text.primary'], raised, 4.5),
      ('text.secondary / base', rgb['text.secondary'], base, 4.5),
      ('text.secondary / panel', rgb['text.secondary'], panel, 4.5),
      ('text.disabled / base (UI glyph)', rgb['text.disabled'], base, 3.0),
      ('accent / base (UI glyph)', rgb['accent'], base, 3.0),
      ('text.on-accent / accent', rgb['text.on-accent'], rgb['accent'], 4.5),
      ('state.playing / base', rgb['state.playing'], base, 3.0),
      ('state.recording / base', rgb['state.recording'], base, 3.0),
      ('state.queued / base', rgb['state.queued'], base, 3.0),
      ('state.playing.dim / base', rgb['state.playing.dim'], base, 3.0),
      ('state.recording.dim / base', rgb['state.recording.dim'], base, 3.0),
    ]
    for i in range(8):
        checks.append((f'track {i} / base', oklch_to_srgb(L, C, track_hue(i)), base, 3.0))
    for label, fg, bg, need in checks:
        r = contrast(fg, bg)
        print(f'{label:34s} {r:5.2f}  need {need}  {"ok" if r >= need else "FAIL"}')
if len(sys.argv) > 1:
    json.dump(out, open(sys.argv[1], 'w'), indent=1)
