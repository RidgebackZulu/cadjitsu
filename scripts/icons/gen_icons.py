#!/usr/bin/env python3
"""Generates Cadly's icon set: resources/icons/*.svg.

Every icon is drawn here from a few shared primitives (isometric boxes with
light/mid/dark faces, orange "operated-on" faces, blue action arrows, soft
contact shadows) so the whole set shares one light source and palette. The
SVGs are committed; run this after changing an icon:

    python3 scripts/icons/gen_icons.py

The artboard is 64 x 64. Only SVG features Qt SVG renders are used
(paths, gradients, opacity; no filters or CSS).
"""
import math
import re
import os
import sys

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "resources", "icons")

INK = "#22324A"          # outlines
ACCENT = "#2F7BE0"       # recolourable accent (constraint glyphs)
C30 = math.cos(math.radians(30))

DEFS = """
<linearGradient id="gTop" x1="0" y1="0" x2="0.3" y2="1"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#D5E3F3"/></linearGradient>
<linearGradient id="gLeft" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#C3D6EC"/><stop offset="1" stop-color="#9FBADA"/></linearGradient>
<linearGradient id="gRight" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8CAAD0"/><stop offset="1" stop-color="#6687B4"/></linearGradient>
<linearGradient id="oTop" x1="0" y1="0" x2="0.3" y2="1"><stop offset="0" stop-color="#FFD58F"/><stop offset="1" stop-color="#FFA63A"/></linearGradient>
<linearGradient id="oSide" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFB24E"/><stop offset="1" stop-color="#E77B12"/></linearGradient>
<linearGradient id="oDark" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#F09432"/><stop offset="1" stop-color="#C9620A"/></linearGradient>
<linearGradient id="bArrow" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#63A9F7"/><stop offset="1" stop-color="#1B5DC4"/></linearGradient>
<linearGradient id="bArrowH" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#63A9F7"/><stop offset="1" stop-color="#1B5DC4"/></linearGradient>
<linearGradient id="green" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#6BD77B"/><stop offset="1" stop-color="#1E9A3A"/></linearGradient>
<linearGradient id="red" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FF7A6B"/><stop offset="1" stop-color="#CF2A22"/></linearGradient>
<linearGradient id="yellow" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFE07A"/><stop offset="1" stop-color="#F0B429"/></linearGradient>
<linearGradient id="yellowDark" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#F5C54A"/><stop offset="1" stop-color="#D99A12"/></linearGradient>
<linearGradient id="paper" x1="0" y1="0" x2="0.4" y2="1"><stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#E4ECF6"/></linearGradient>
<linearGradient id="steel" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#E8EEF5"/><stop offset="1" stop-color="#9AAABE"/></linearGradient>
<linearGradient id="dark" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5C6E86"/><stop offset="1" stop-color="#2A3950"/></linearGradient>
<linearGradient id="bore" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3E5270"/><stop offset="1" stop-color="#8EA6C6"/></linearGradient>
<linearGradient id="glass" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFFFFF" stop-opacity="0.95"/><stop offset="1" stop-color="#BFD9F7" stop-opacity="0.75"/></linearGradient>
<linearGradient id="cut" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FF8A6E"/><stop offset="1" stop-color="#E04A2A"/></linearGradient>
<linearGradient id="planeFill" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FFC56B" stop-opacity="0.85"/><stop offset="1" stop-color="#F58A1F" stop-opacity="0.55"/></linearGradient>
<radialGradient id="shadow" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#0B1A30" stop-opacity="0.34"/><stop offset="1" stop-color="#0B1A30" stop-opacity="0"/></radialGradient>
<radialGradient id="iris" cx="0.4" cy="0.35" r="0.65"><stop offset="0" stop-color="#8CC4FF"/><stop offset="0.6" stop-color="#2F7BE0"/><stop offset="1" stop-color="#174A9A"/></radialGradient>
<radialGradient id="sphere" cx="0.35" cy="0.3" r="0.75"><stop offset="0" stop-color="#FFFFFF"/><stop offset="0.5" stop-color="#BCD2EC"/><stop offset="1" stop-color="#6F8FBA"/></radialGradient>
"""


BBOX = [1e9, 1e9, -1e9, -1e9]


def extent(x, y, pad=0.0):
    BBOX[0] = min(BBOX[0], x - pad)
    BBOX[1] = min(BBOX[1], y - pad)
    BBOX[2] = max(BBOX[2], x + pad)
    BBOX[3] = max(BBOX[3], y + pad)


def path_extent(d, pad):
    """Tracks a path's points (curve midpoints for Q, sampled arcs for A)."""
    import re
    toks = re.findall(r"[MLQAZ]|-?\d*\.?\d+", d)
    i, cmd, cur = 0, "M", (0.0, 0.0)
    num = lambda k: float(toks[k])
    while i < len(toks):
        t = toks[i]
        if t in "MLQAZ":
            cmd = t
            i += 1
            continue
        if cmd in "ML":
            cur = (num(i), num(i + 1))
            extent(*cur, pad)
            i += 2
        elif cmd == "Q":
            c, e = (num(i), num(i + 1)), (num(i + 2), num(i + 3))
            extent(0.25 * cur[0] + 0.5 * c[0] + 0.25 * e[0], 0.25 * cur[1] + 0.5 * c[1] + 0.25 * e[1], pad)
            cur = e
            extent(*cur, pad)
            i += 4
        elif cmd == "A":
            r, large, sweep, e = num(i), int(num(i + 3)), int(num(i + 4)), (num(i + 5), num(i + 6))
            # Centre of the circular arc from cur to e.
            mx, my = (cur[0] + e[0]) / 2, (cur[1] + e[1]) / 2
            dx, dy = e[0] - cur[0], e[1] - cur[1]
            q = math.hypot(dx, dy)
            if q > 1e-9 and r >= q / 2:
                h = math.sqrt(max(0.0, r * r - q * q / 4))
                sgn = 1 if large != sweep else -1
                cx, cy = mx - sgn * h * dy / q, my + sgn * h * dx / q
                a0 = math.atan2(cur[1] - cy, cur[0] - cx)
                a1 = math.atan2(e[1] - cy, e[0] - cx)
                da = a1 - a0
                if sweep and da < 0:
                    da += 2 * math.pi
                if not sweep and da > 0:
                    da -= 2 * math.pi
                for k in range(25):
                    a = a0 + da * k / 24
                    extent(cx + r * math.cos(a), cy + r * math.sin(a), pad)
            cur = e
            extent(*cur, pad)
            i += 7
        else:
            i += 1


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def pts(points):
    return " ".join(f"{f(x)},{f(y)}" for x, y in points)


def poly(points, fill, stroke=INK, sw=1.6, extra=""):
    for x, y in points:
        extent(x, y, sw / 2 if stroke else 0)
    s = f' stroke="{stroke}" stroke-width="{f(sw)}" stroke-linejoin="round"' if stroke else ""
    return f'<polygon points="{pts(points)}" fill="{fill}"{s} {extra}/>'


def path(d, fill="none", stroke=INK, sw=1.6, extra=""):
    path_extent(d, sw / 2 if stroke else 0)
    s = f' stroke="{stroke}" stroke-width="{f(sw)}" stroke-linejoin="round" stroke-linecap="round"' if stroke else ""
    return f'<path d="{d}" fill="{fill}"{s} {extra}/>'


def line(a, b, stroke=INK, sw=1.6, extra=""):
    extent(*a, sw / 2)
    extent(*b, sw / 2)
    return (f'<line x1="{f(a[0])}" y1="{f(a[1])}" x2="{f(b[0])}" y2="{f(b[1])}" stroke="{stroke}" '
            f'stroke-width="{f(sw)}" stroke-linecap="round" {extra}/>')


def circle(c, r, fill, stroke=INK, sw=1.6, extra=""):
    extent(c[0], c[1], r + (sw / 2 if stroke else 0))
    s = f' stroke="{stroke}" stroke-width="{f(sw)}"' if stroke else ""
    return f'<circle cx="{f(c[0])}" cy="{f(c[1])}" r="{f(r)}" fill="{fill}"{s} {extra}/>'


def ellipse(c, rx, ry, fill, stroke=None, sw=1.6, extra=""):
    s = f' stroke="{stroke}" stroke-width="{f(sw)}"' if stroke else ""
    return f'<ellipse cx="{f(c[0])}" cy="{f(c[1])}" rx="{f(rx)}" ry="{f(ry)}" fill="{fill}"{s} {extra}/>'


def shadow(cx, cy, rx, ry=None):
    return ellipse((cx, cy), rx, ry if ry else rx * 0.32, "url(#shadow)")


class Iso:
    """Isometric projection: +x to the lower right, +y to the lower left, z up."""

    def __init__(self, ox=32, oy=34, s=1.0):
        self.ox, self.oy, self.s = ox, oy, s

    def p(self, x, y, z=0.0):
        return (self.ox + (x - y) * C30 * self.s, self.oy + (x + y) * 0.5 * self.s - z * self.s)

    def box(self, x0, y0, z0, dx, dy, dz, top="url(#gTop)", left="url(#gLeft)", right="url(#gRight)",
            sw=1.6, highlight=True):
        x1, y1, z1 = x0 + dx, y0 + dy, z0 + dz
        P = self.p
        out = [
            poly([P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)], left, sw=sw),   # +y face
            poly([P(x1, y0, z0), P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1)], right, sw=sw),  # +x face
            poly([P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)], top, sw=sw),    # top
        ]
        if highlight:
            out.append(line(P(x0 + 0.6, y1, z1 - 0.2), P(x1, y1, z1 - 0.2), "#FFFFFF", 0.9, 'stroke-opacity="0.75"'))
            out.append(line(P(x1, y1 - 0.4, z1 - 0.2), P(x1, y0 + 0.6, z1 - 0.2), "#FFFFFF", 0.9,
                            'stroke-opacity="0.6"'))
        return "".join(out)

    def top_ellipse(self, cx, cy, z, r, n=48):
        return [self.p(cx + r * math.cos(t), cy + r * math.sin(t), z) for t in
                (2 * math.pi * i / n for i in range(n))]

    def floor_shadow(self, x0, y0, dx, dy, grow=1.15):
        c = self.p(x0 + dx / 2, y0 + dy / 2, 0)
        w = (dx + dy) * C30 * self.s * 0.5 * grow
        return ellipse((c[0], c[1] + 1.5), w * 1.05, w * 0.42, "url(#shadow)")


def arrow_up(cx, top, bottom, w=10, head=9, fill="url(#bArrow)"):
    """A fat vertical arrow pointing up."""
    hw, sw = w / 2, w / 4.2
    d = (f"M{f(cx)},{f(top)} L{f(cx + hw)},{f(top + head)} L{f(cx + sw)},{f(top + head)} "
         f"L{f(cx + sw)},{f(bottom)} L{f(cx - sw)},{f(bottom)} L{f(cx - sw)},{f(top + head)} "
         f"L{f(cx - hw)},{f(top + head)} Z")
    return (path(d, fill, "#153F86", 1.3) +
            line((cx - sw + 1.1, top + head + 1), (cx - sw + 1.1, bottom - 1), "#FFFFFF", 0.9,
                 'stroke-opacity="0.55"'))


def arrow_poly(points, fill="url(#bArrow)", stroke="#153F86"):
    return poly(points, fill, stroke, 1.3)


def curved_arrow(cx, cy, r, a0, a1, width=5.5, head=8.5, fill="url(#bArrow)", ccw=False):
    """A thick arc arrow from angle a0 to a1 (degrees, screen: 0 = right, 90 = down)."""
    rad = math.radians
    ro, ri = r + width / 2, r - width / 2
    # Arrowhead at a1.
    sweep = 1 if a1 > a0 else 0
    large = 1 if abs(a1 - a0) > 180 else 0
    headAng = math.degrees(head / r) * (1 if a1 > a0 else -1)
    e = a1 - headAng
    P = lambda rr, a: (cx + rr * math.cos(rad(a)), cy + rr * math.sin(rad(a)))
    o0, o1, i1, i0 = P(ro, a0), P(ro, e), P(ri, e), P(ri, a0)
    tip = P(r, a1)
    h1, h2 = P(r + width * 1.15, e), P(r - width * 1.15, e)
    d = (f"M{f(o0[0])},{f(o0[1])} A{f(ro)},{f(ro)} 0 {large} {sweep} {f(o1[0])},{f(o1[1])} "
         f"L{f(h1[0])},{f(h1[1])} L{f(tip[0])},{f(tip[1])} L{f(h2[0])},{f(h2[1])} "
         f"L{f(i1[0])},{f(i1[1])} A{f(ri)},{f(ri)} 0 {large} {1 - sweep} {f(i0[0])},{f(i0[1])} Z")
    return path(d, fill, "#153F86", 1.3)


def badge(cx, cy, r, fill, symbol):
    """A round badge (bottom-right status) with a white symbol."""
    s = circle((cx, cy), r, fill, "#FFFFFF", 1.6)
    k = r * 0.5
    if symbol == "plus":
        s += line((cx - k, cy), (cx + k, cy), "#FFFFFF", 2.4) + line((cx, cy - k), (cx, cy + k), "#FFFFFF", 2.4)
    elif symbol == "check":
        s += path(f"M{f(cx - k)},{f(cy)} L{f(cx - k * 0.2)},{f(cy + k * 0.75)} L{f(cx + k)},{f(cy - k * 0.7)}",
                  "none", "#FFFFFF", 2.6)
    elif symbol == "x":
        s += line((cx - k * 0.8, cy - k * 0.8), (cx + k * 0.8, cy + k * 0.8), "#FFFFFF", 2.4)
        s += line((cx - k * 0.8, cy + k * 0.8), (cx + k * 0.8, cy - k * 0.8), "#FFFFFF", 2.4)
    elif symbol == "down":
        s += path(f"M{f(cx)},{f(cy - k)} L{f(cx)},{f(cy + k * 0.8)} M{f(cx - k * 0.7)},{f(cy + k * 0.1)} "
                  f"L{f(cx)},{f(cy + k * 0.85)} L{f(cx + k * 0.7)},{f(cy + k * 0.1)}", "none", "#FFFFFF", 2.2)
    elif symbol == "minus":
        s += line((cx - k, cy), (cx + k, cy), "#FFFFFF", 2.6)
    return s


def sketch_point(c, r=3.2, fill="#FFFFFF", stroke=INK):
    return circle(c, r, fill, stroke, 1.5)


def sheet(x, y, w, h, fold=10, fill="url(#paper)"):
    d = (f"M{f(x)},{f(y)} L{f(x + w - fold)},{f(y)} L{f(x + w)},{f(y + fold)} L{f(x + w)},{f(y + h)} "
         f"L{f(x)},{f(y + h)} Z")
    fd = f"M{f(x + w - fold)},{f(y)} L{f(x + w - fold)},{f(y + fold)} L{f(x + w)},{f(y + fold)}"
    return (ellipse((x + w / 2, y + h + 1.5), w * 0.55, 2.6, "url(#shadow)") + path(d, fill, INK, 1.6) +
            path(fd, "#C9D6E6", INK, 1.3))


def iso_sheet(iso, x0, y0, dx, dy, z=0, grid=True, fill="url(#paper)"):
    P = iso.p
    out = poly([P(x0, y0, z), P(x0 + dx, y0, z), P(x0 + dx, y0 + dy, z), P(x0, y0 + dy, z)], fill, sw=1.5)
    if grid:
        for i in range(1, 4):
            t = i / 4
            out += line(P(x0 + dx * t, y0, z), P(x0 + dx * t, y0 + dy, z), "#9FB6D3", 0.8)
            out += line(P(x0, y0 + dy * t, z), P(x0 + dx, y0 + dy * t, z), "#9FB6D3", 0.8)
    return out


def pencil(x, y, length=34, angle=-45, w=7.5):
    """A pencil whose tip is at (x, y), pointing along `angle` (towards the tip)."""
    a = math.radians(angle)
    ux, uy = math.cos(a), math.sin(a)          # tip direction
    nx, ny = -uy, ux
    tipLen, eraser = 8.5, 6.5
    P = lambda t, s: (x - ux * t + nx * s, y - uy * t + ny * s)
    hw = w / 2
    body = [P(tipLen, hw), P(length - eraser, hw), P(length - eraser, -hw), P(tipLen, -hw)]
    ferr = [P(length - eraser, hw), P(length - eraser + 2.5, hw), P(length - eraser + 2.5, -hw),
            P(length - eraser, -hw)]
    rub = [P(length - eraser + 2.5, hw), P(length, hw), P(length, -hw), P(length - eraser + 2.5, -hw)]
    wood = [P(0, 0), P(tipLen, hw), P(tipLen, -hw)]
    lead = [P(0, 0), P(3.2, hw * 0.38), P(3.2, -hw * 0.38)]
    return (poly(body, "url(#yellow)", INK, 1.4) + line(P(tipLen + 1, hw * 0.1), P(length - eraser - 1, hw * 0.1),
                                                          "#FFF3C4", 1.0) +
            poly(ferr, "url(#steel)", INK, 1.2) + poly(rub, "#F29AA7", INK, 1.2) +
            poly(wood, "#F6D7A8", INK, 1.2) + poly(lead, "#33404F", None))


# --------------------------------------------------------------------------------------------
# Modelling

def extrude():
    iso = Iso(32, 38, 1.05)
    s = iso.floor_shadow(-11, -11, 22, 22)
    s += iso.box(-11, -11, 0, 22, 22, 11, top="url(#oTop)", left="url(#gLeft)", right="url(#gRight)")
    # Ghost of the profile it came from.
    c = iso.p(0, 0, 11)
    s += arrow_up(c[0], 3, c[1] - 1, w=13, head=10)
    return s


def fillet():
    iso = Iso(31, 40, 1.08)
    P = iso.p
    x0, y0, dx, dy, dz, r = -12, -12, 24, 24, 16, 9
    x1, y1, z1 = x0 + dx, y0 + dy, dz
    s = iso.floor_shadow(x0, y0, dx, dy)
    arc = lambda y: [P(x1 - r + r * math.sin(t), y, z1 - r + r * math.cos(t))
                     for t in (math.pi / 2 * i / 12 for i in range(13))]
    # +y face with the rounded corner.
    left = [P(x0, y1, 0), P(x1, y1, 0)] + list(reversed(arc(y1))) + [P(x0, y1, z1)]
    s += poly(left, "url(#gLeft)")
    s += poly([P(x1, y0, 0), P(x1, y1, 0), P(x1, y1, z1 - r), P(x1, y0, z1 - r)], "url(#gRight)")
    s += poly(arc(y0) + list(reversed(arc(y1))), "url(#oSide)")
    s += poly([P(x0, y0, z1), P(x1 - r, y0, z1), P(x1 - r, y1, z1), P(x0, y1, z1)], "url(#gTop)")
    s += line(P(x1 - r + 1, y1 - 0.5, z1 - 0.3), P(x1 - r + 1, y0 + 0.8, z1 - 0.3), "#FFFFFF", 1.1,
              'stroke-opacity="0.8"')
    return s


def chamfer():
    iso = Iso(31, 40, 1.08)
    P = iso.p
    x0, y0, dx, dy, dz, c = -12, -12, 24, 24, 16, 8
    x1, y1, z1 = x0 + dx, y0 + dy, dz
    s = iso.floor_shadow(x0, y0, dx, dy)
    s += poly([P(x0, y1, 0), P(x1, y1, 0), P(x1, y1, z1 - c), P(x1 - c, y1, z1), P(x0, y1, z1)], "url(#gLeft)")
    s += poly([P(x1, y0, 0), P(x1, y1, 0), P(x1, y1, z1 - c), P(x1, y0, z1 - c)], "url(#gRight)")
    s += poly([P(x1 - c, y0, z1), P(x1, y0, z1 - c), P(x1, y1, z1 - c), P(x1 - c, y1, z1)], "url(#oTop)")
    s += poly([P(x0, y0, z1), P(x1 - c, y0, z1), P(x1 - c, y1, z1), P(x0, y1, z1)], "url(#gTop)")
    return s


def hole():
    iso = Iso(32, 36, 1.0)
    P = iso.p
    s = iso.floor_shadow(-15, -15, 30, 30)
    s += iso.box(-15, -15, 0, 30, 30, 9)
    rim = iso.top_ellipse(0, 0, 9, 9)
    # The bore: dark far wall, lit near wall.
    s += poly(rim, "url(#bore)", INK, 1.6)
    far = rim[24:] + rim[:1]
    wall = far + [(x, y + 5.5) for x, y in reversed(far)]
    s += poly(wall, "#22324A", None, 0, 'fill-opacity="0.55"')
    near = rim[:25]
    s += path("M" + " L".join(f"{f(x)},{f(y - 1.2)}" for x, y in near[3:22]), "none", "#FFFFFF", 1.2,
              'stroke-opacity="0.7"')
    s += poly(rim, "none", INK, 1.6)
    # Drill axis and arrow.
    c = P(0, 0, 9)
    s += line((c[0], 3), (c[0], c[1] - 9), ACCENT_AXIS, 1.8, 'stroke-dasharray="3.2,2.4"')
    s += arrow_poly([(c[0], c[1] - 1), (c[0] - 5.5, c[1] - 10), (c[0] + 5.5, c[1] - 10)])
    return s


ACCENT_AXIS = "#1B5DC4"


def combine():
    iso = Iso(30, 37, 0.95)
    s = iso.floor_shadow(-14, -8, 30, 24, 1.1)
    s += iso.box(-14, -8, 0, 18, 18, 16)
    s += iso.box(-2, -2, 0, 18, 18, 11, top="url(#oTop)", left="url(#oSide)", right="url(#oDark)")
    s += badge(50, 50, 9, "url(#green)", "plus")
    return s


def plane():
    iso = Iso(32, 40, 1.0)
    P = iso.p
    s = iso.floor_shadow(-14, -14, 28, 28)
    s += iso.box(-14, -14, 0, 28, 28, 6)
    z = 20
    s += poly([P(-18, -18, z), P(18, -18, z), P(18, 18, z), P(-18, 18, z)], "url(#planeFill)", "#C8620C", 1.5)
    a, b = P(0, 0, 6), P(0, 0, z)
    s += line(a, (b[0], b[1] + 6), ACCENT_AXIS, 2.4)
    s += arrow_poly([b, (b[0] - 4.2, b[1] + 7.5), (b[0] + 4.2, b[1] + 7.5)])
    return s


def section():
    iso = Iso(33, 38, 1.0)
    P = iso.p
    x0, y0, dx, dy, dz = -15, -15, 30, 30, 20
    xc = 0  # cut plane x = 0: keep x0..xc
    s = iso.floor_shadow(x0, y0, dx, dy)
    # Ghost of the removed half.
    s += poly([P(xc, y0, dz), P(x0 + dx, y0, dz), P(x0 + dx, y0 + dy, dz), P(xc, y0 + dy, dz)], "#DCE7F4", "#8CA5C5",
              1.1, 'stroke-dasharray="2.5,2" fill-opacity="0.5"')
    s += poly([P(x0 + dx, y0, 0), P(x0 + dx, y0 + dy, 0), P(x0 + dx, y0 + dy, dz), P(x0 + dx, y0, dz)], "none",
              "#8CA5C5", 1.1, 'stroke-dasharray="2.5,2"')
    s += poly([P(x0, y0 + dy, 0), P(xc, y0 + dy, 0), P(xc, y0 + dy, dz), P(x0, y0 + dy, dz)], "url(#gLeft)")
    s += poly([P(x0, y0, dz), P(xc, y0, dz), P(xc, y0 + dy, dz), P(x0, y0 + dy, dz)], "url(#gTop)")
    # The cut face with hatching.
    cutFace = [P(xc, y0, 0), P(xc, y0 + dy, 0), P(xc, y0 + dy, dz), P(xc, y0, dz)]
    s += poly(cutFace, "url(#cut)")
    for k in range(1, 7):
        t = k / 7
        a = P(xc, y0 + dy * t, 0) if t <= 1 else None
        s += line(P(xc, y0 + dy * min(1, t), dz * max(0, 0)), P(xc, y0, dz * t), "#FFFFFF", 0.9,
                  'stroke-opacity="0.55"')
    s += poly(cutFace, "none", "#8E1E0E", 1.5)
    return s


def measure():
    s = shadow(33, 52, 22, 4)
    a = math.radians(-30)
    rot = lambda x, y: (32 + (x - 32) * math.cos(a) - (y - 34) * math.sin(a),
                        34 + (x - 32) * math.sin(a) + (y - 34) * math.cos(a))
    s += poly([rot(6, 26), rot(58, 26), rot(58, 41), rot(6, 41)], "url(#yellow)", INK, 1.6)
    for i in range(1, 12):
        x = 6 + i * 52 / 12
        h = 7 if i % 3 == 0 else 4.2
        s += line(rot(x, 26), rot(x, 26 + h), INK, 1.3)
    s += line(rot(8, 39), rot(56, 39), "#FFF6CF", 1.0)
    s += line((13, 11), (45, 11), ACCENT_AXIS, 2)
    s += arrow_poly([(9, 11), (16, 7), (16, 15)])
    s += arrow_poly([(49, 11), (42, 7), (42, 15)])
    return s


def overhang():
    """A mushroom block: a wide cap on a narrow stem. The cap's underside
    overhangs, so red support struts hold it up from the build plate."""
    iso = Iso(32, 38, 1.0)
    P = iso.p
    s = iso.floor_shadow(-15, -15, 30, 30)
    # Supports under the cap's visible corners (behind the stem where hidden).
    for (x, y) in ((15, -13), (15, 15), (-13, 15), (15, 1), (1, 15)):
        s += line(P(x, y, 0), P(x, y, 18), "#D8322A", 1.7, 'stroke-dasharray="2.6,1.8"')
    s += iso.box(-6, -6, 0, 12, 12, 18)
    s += iso.box(-15, -15, 18, 30, 30, 7)
    # The cap's lower edges, where support is needed.
    s += line(P(-15, 15, 18), P(15, 15, 18), "#D8322A", 2.6)
    s += line(P(15, 15, 18), P(15, -15, 18), "#D8322A", 2.6)
    return s


def split():
    """A block split in two by a plane: the halves pulled apart, the cutting
    plane glowing orange between them."""
    iso = Iso(32, 38, 1.0)
    P = iso.p
    s = iso.floor_shadow(-18, -12, 36, 24)
    s += iso.box(-18, -10, 0, 14, 20, 16)
    # The cutting plane (x = 0), standing between the halves.
    s += poly([P(0, -15, -3), P(0, 15, -3), P(0, 15, 21), P(0, -15, 21)], "url(#planeFill)", "#D9771A", 1.3)
    s += iso.box(4, -10, 0, 14, 20, 16, top="url(#oTop)", left="url(#oSide)", right="url(#oDark)")
    return s


def move_tool():
    """A sketch rectangle and its moved copy, with a four-way move arrow."""
    s = sheet(6, 10, 52, 44, fold=9)
    s += poly([(12, 34), (30, 34), (30, 48), (12, 48)], "none", "#9DB0C8", 1.6, 'stroke-dasharray="3,2"')
    s += poly([(30, 18), (48, 18), (48, 32), (30, 32)], "none", ACCENT_AXIS, 2.4)
    for pnt in ((30, 18), (48, 18), (48, 32), (30, 32)):
        s += sketch_point(pnt, 2.4)
    # Four-way arrow at the moved shape's corner.
    cx, cy, r, h = 22, 26, 9, 4.2
    s += line((cx - r + 2, cy), (cx + r - 2, cy), "#1B5DC4", 2.2)
    s += line((cx, cy - r + 2), (cx, cy + r - 2), "#1B5DC4", 2.2)
    for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        tip = (cx + dx * r, cy + dy * r)
        base = (cx + dx * (r - h * 1.3), cy + dy * (r - h * 1.3))
        side = (-dy * h, dx * h)
        s += arrow_poly([tip, (base[0] + side[0], base[1] + side[1]), (base[0] - side[0], base[1] - side[1])])
    return s


def select_tool():
    """The select pointer: an arrow cursor over a dashed selection box."""
    s = poly([(8, 10), (40, 10), (40, 34), (8, 34)], "#E6EEF9", "#6F84A0", 1.4,
             'stroke-dasharray="3,2"')
    arrow = [(24, 18), (24, 54), (32.5, 46), (38, 58), (44, 55.5), (38.5, 44), (50, 44)]
    s += poly(arrow, "#FFFFFF", INK, 2.2)
    return s


def offset():
    """A rounded sketch outline and its offset copy around it, with the gap's
    distance arrow."""
    def rounded(x0, y0, x1, y1, r):
        return (f"M{f(x0 + r)},{f(y0)} L{f(x1 - r)},{f(y0)} A{f(r)},{f(r)} 0 0 1 {f(x1)},{f(y0 + r)} "
                f"L{f(x1)},{f(y1 - r)} A{f(r)},{f(r)} 0 0 1 {f(x1 - r)},{f(y1)} L{f(x0 + r)},{f(y1)} "
                f"A{f(r)},{f(r)} 0 0 1 {f(x0)},{f(y1 - r)} L{f(x0)},{f(y0 + r)} A{f(r)},{f(r)} 0 0 1 {f(x0 + r)},{f(y0)} Z")
    s = sheet(6, 10, 52, 44, fold=9)
    s += path(rounded(20, 24, 40, 40, 4), "none", "#6F84A0", 1.8)
    s += path(rounded(12, 16, 48, 48, 9), "none", ACCENT_AXIS, 2.4)
    # The offset distance, from the inner outline out to the offset one.
    s += line((40, 32), (48, 32), "#1B5DC4", 1.6)
    s += arrow_poly([(48, 32), (44.2, 29.6), (44.2, 34.4)])
    s += arrow_poly([(40, 32), (43.8, 29.6), (43.8, 34.4)])
    return s


def draft():
    """A block whose right wall leans in over a hinge along its foot (orange),
    with the tilt shown by a curved arrow."""
    iso = Iso(30, 38, 1.0)
    P = iso.p
    x0, y0, dy, h = -16, -12, 24, 22
    xb, xt = 12, 3  # the right wall's foot and top: it leans in
    s = iso.floor_shadow(x0, y0, xb - x0, dy)
    s += poly([P(x0, y0 + dy, 0), P(xb, y0 + dy, 0), P(xt, y0 + dy, h), P(x0, y0 + dy, h)], "url(#gLeft)")
    s += poly([P(xb, y0, 0), P(xb, y0 + dy, 0), P(xt, y0 + dy, h), P(xt, y0, h)], "url(#oSide)")
    s += poly([P(x0, y0, h), P(xt, y0, h), P(xt, y0 + dy, h), P(x0, y0 + dy, h)], "url(#gTop)")
    # The hinge.
    s += line(P(xb, y0, 0), P(xb, y0 + dy, 0), "#C9620A", 2.6)
    # The upright it leaned from, dashed.
    s += line(P(xb, y0 + dy, 0), P(xb, y0 + dy, h), "#8CA5C5", 1.2, 'stroke-dasharray="2.5,2"')
    s += curved_arrow(P(xb, y0 + dy, 0)[0], P(xb, y0 + dy, 0)[1], 17, 245, 272, 4.5, 6.5)
    return s


def mirror():
    """A body and its mirror image either side of an orange mirror plane."""
    iso = Iso(32, 38, 1.0)
    P = iso.p
    s = iso.floor_shadow(-20, -10, 40, 20)
    # The original: an L-shaped block (left), the plane, and its reflection (right).
    s += iso.box(-20, -8, 0, 12, 16, 8)
    s += iso.box(-20, -8, 8, 5, 16, 10)
    s += poly([P(0, -14, -2), P(0, 14, -2), P(0, 14, 22), P(0, -14, 22)], "url(#planeFill)", "#D9771A", 1.3)
    s += iso.box(8, -8, 0, 12, 16, 8, top="url(#oTop)", left="url(#oSide)", right="url(#oDark)")
    s += iso.box(15, -8, 8, 5, 16, 10, top="url(#oTop)", left="url(#oSide)", right="url(#oDark)")
    return s


def pattern_rect():
    """A plate with a grid of pegs: the first one blue, its copies orange."""
    iso = Iso(32, 40, 1.0)
    s = iso.floor_shadow(-20, -14, 40, 28)
    s += iso.box(-20, -14, 0, 40, 28, 4)
    for j in range(2):
        for i in range(3):
            x, y = -15 + i * 12, -9 + j * 12
            first = i == 0 and j == 0
            s += iso.box(x, y, 4, 6, 6, 9,
                         top="url(#gTop)" if first else "url(#oTop)",
                         left="url(#gLeft)" if first else "url(#oSide)",
                         right="url(#gRight)" if first else "url(#oDark)", sw=1.3)
    return s


def pattern_circ():
    """A disc with a bolt circle of holes: the first one blue, its copies orange."""
    iso = Iso(32, 36, 1.0)
    P = iso.p
    s = iso.floor_shadow(-20, -20, 40, 40)
    rim_top = iso.top_ellipse(0, 0, 6, 20)
    rim_bot = iso.top_ellipse(0, 0, 0, 20)
    # Side band: bottom half of the lower rim, joined to the upper rim.
    lo = [p for p in rim_bot if p[1] >= P(0, 0, 0)[1] - 0.1]
    s += poly(rim_top + lo[::-1], "url(#gRight)")
    s += poly(rim_top, "url(#gTop)")
    for k in range(6):
        a = 2 * math.pi * k / 6 + 0.3
        cx, cy = 12 * math.cos(a), 12 * math.sin(a)
        hole = iso.top_ellipse(cx, cy, 6, 3.2, 24)
        s += poly(hole, "url(#bore)" if k == 0 else "url(#oDark)", "#153F86" if k == 0 else "#8E4A0A", 1.2)
    s += curved_arrow(32, 36, 23, 200, 320, 4.5, 7)
    return s


def thread():
    """A threaded rod standing in a nut-like block: crests drawn as slanted
    rings, the thread orange."""
    iso = Iso(32, 44, 1.0)
    P = iso.p
    s = iso.floor_shadow(-14, -14, 28, 28)
    s += iso.box(-14, -14, 0, 28, 28, 7)
    r, h = 7.5, 26
    top = iso.top_ellipse(0, 0, 7 + h, r)
    base = iso.top_ellipse(0, 0, 7, r)
    lo = [p for p in base if p[1] >= P(0, 0, 7)[1] - 0.1]
    left = min(top, key=lambda p: p[0])
    right = max(top, key=lambda p: p[0])
    # The rod's side, then its thread crests (front halves of slanted rings).
    s += poly([left] + [p for p in top if p[1] >= P(0, 0, 7 + h)[1]] + [right] + lo[::-1], "url(#oSide)", "#8E4A0A", 1.4)
    for k in range(6):
        z = 9 + k * 4
        ring = [P(r * math.cos(t), r * math.sin(t), z + 1.6 * math.sin(t) + 1.6)
                for t in (math.pi * i / 16 for i in range(-4, 21))]
        front = [q for q in ring if q[1] >= P(0, 0, z)[1] - 3.5]
        s += path("M" + " L".join(f"{f(x)},{f(y)}" for x, y in front), "none", "#8E4A0A", 1.3)
    s += poly(top, "url(#oTop)", "#8E4A0A", 1.4)
    s += ellipse(P(0, 0, 7 + h), r * 0.45, r * 0.26, "#C9620A")
    return s


def flip():
    iso = Iso(32, 44, 1.0)
    s = iso.floor_shadow(-13, -13, 26, 26)
    s += iso.box(-13, -13, 0, 26, 26, 6)
    s += curved_arrow(32, 30, 20, 195, 345, 6, 10)
    return s


# --------------------------------------------------------------------------------------------
# Sketching

def sketch():
    iso = Iso(28, 36, 1.0)
    s = iso.floor_shadow(-15, -15, 30, 30, 1.0)
    s += iso_sheet(iso, -17, -17, 34, 34)
    P = iso.p
    curve = [P(-11 + 22 * t, 9 - 18 * t + 9 * math.sin(math.pi * t) * 0.9, 0) for t in (i / 16 for i in range(17))]
    s += path("M" + " L".join(f"{f(x)},{f(y)}" for x, y in curve), "none", ACCENT_AXIS, 2.4)
    s += pencil(curve[-1][0], curve[-1][1], 36, -55, 8)
    return s


def finish_sketch():
    iso = Iso(28, 34, 1.0)
    s = iso.floor_shadow(-15, -15, 30, 30, 1.0)
    s += iso_sheet(iso, -17, -17, 34, 34)
    s += badge(46, 44, 13, "url(#green)", "check")
    return s


def line_tool():
    s = line((13, 49), (51, 15), ACCENT, 3.4)
    s += sketch_point((13, 49), 4.2) + sketch_point((51, 15), 4.2)
    return s


def rectangle():
    s = '<rect x="11" y="16" width="42" height="32" fill="%s" fill-opacity="0.12" stroke="%s" stroke-width="3.2"/>' % (
        ACCENT, ACCENT)
    for c in [(11, 16), (53, 16), (53, 48), (11, 48)]:
        s += sketch_point(c, 4)
    return s


def center_rectangle():
    s = '<rect x="11" y="16" width="42" height="32" fill="%s" fill-opacity="0.12" stroke="%s" stroke-width="3.2"/>' % (
        ACCENT, ACCENT)
    s += line((11, 16), (53, 48), "#7F93AD", 1.3, 'stroke-dasharray="3,2.4"')
    s += line((53, 16), (11, 48), "#7F93AD", 1.3, 'stroke-dasharray="3,2.4"')
    s += sketch_point((32, 32), 4.2, ACCENT, INK)
    for c in [(53, 16), (11, 48)]:
        s += sketch_point(c, 3.6)
    return s


def circle_tool():
    s = circle((32, 32), 20, ACCENT, ACCENT, 3.2, 'fill-opacity="0.12"')
    s += line((32, 32), (46.1, 17.9), "#7F93AD", 1.4, 'stroke-dasharray="3,2.4"')
    s += sketch_point((32, 32), 3.8, ACCENT, INK) + sketch_point((46.1, 17.9), 3.6)
    return s


def arc():
    s = path("M10,46 A24,24 0 0 1 54,46", "none", ACCENT, 3.4)
    s += line((32, 46), (10, 46), "#7F93AD", 1.3, 'stroke-dasharray="3,2.4"')
    s += line((32, 46), (54, 46), "#7F93AD", 1.3, 'stroke-dasharray="3,2.4"')
    s += sketch_point((10, 46), 4) + sketch_point((54, 46), 4) + sketch_point((32, 22), 3.6)
    s += sketch_point((32, 46), 2.8, ACCENT, INK)
    return s


def point():
    s = line((32, 12), (32, 22), "#7F93AD", 1.8) + line((32, 42), (32, 52), "#7F93AD", 1.8)
    s += line((12, 32), (22, 32), "#7F93AD", 1.8) + line((42, 32), (52, 32), "#7F93AD", 1.8)
    s += circle((32, 32), 7.5, ACCENT, INK, 1.8)
    s += circle((29.8, 29.8), 2.2, "#FFFFFF", None, 0, 'fill-opacity="0.8"')
    return s


def dimension():
    s = line((12, 50), (12, 20), INK, 1.6) + line((52, 50), (52, 20), INK, 1.6)
    s += line((16, 28), (48, 28), ACCENT, 2.2)
    s += poly([(12.5, 28), (20.5, 23.8), (20.5, 32.2)], ACCENT, None)
    s += poly([(51.5, 28), (43.5, 23.8), (43.5, 32.2)], ACCENT, None)
    s += '<rect x="22" y="12" width="20" height="11" rx="2.5" fill="#FFFFFF" stroke="%s" stroke-width="1.4"/>' % INK
    s += line((26, 17.5), (38, 17.5), INK, 1.8)
    s += line((12, 50), (52, 50), "#B8C6D8", 1.2)
    return s


def construction():
    s = line((12, 50), (52, 14), "#F07A12", 3.2, 'stroke-dasharray="6,4"')
    s += sketch_point((12, 50), 4.2) + sketch_point((52, 14), 4.2)
    return s


def look_at():
    iso = Iso(38, 38, 1.0)
    P = iso.p
    s = poly([P(-12, -12, 0), P(12, -12, 0), P(12, 12, 0), P(-12, 12, 0)], "url(#oTop)", INK, 1.6)
    s += shadow(38, 52, 16, 3)
    c = P(0, 0, 0)
    # Viewing arrow onto the face (from the upper left, as the camera would).
    s += line((8, 8), (c[0] - 4.5, c[1] - 4.5), ACCENT_AXIS, 2.8)
    s += arrow_poly([(c[0] - 1.5, c[1] - 1.5), (c[0] - 10.5, c[1] - 4.5), (c[0] - 4.5, c[1] - 10.5)])
    s += circle((8, 8), 4.5, "url(#iris)", INK, 1.3)
    return s


# --------------------------------------------------------------------------------------------
# Constraint glyphs: flat symbols in the recolourable accent, readable at 14-16 px.

def g_coincident():
    return (line((8, 52), (32, 32), INK, 3) + line((56, 50), (32, 32), INK, 3) +
            circle((32, 32), 9, ACCENT, INK, 2.2) + circle((29, 29), 2.4, "#FFFFFF", None, 0, 'fill-opacity="0.75"'))


def g_horizontal():
    return (line((8, 32), (56, 32), ACCENT, 7) + line((8, 22), (8, 42), INK, 3.5) + line((56, 22), (56, 42), INK, 3.5))


def g_vertical():
    return (line((32, 8), (32, 56), ACCENT, 7) + line((22, 8), (42, 8), INK, 3.5) + line((22, 56), (42, 56), INK, 3.5))


def g_hv():
    return (line((12, 50), (52, 50), ACCENT, 6.5) + line((12, 50), (12, 10), ACCENT, 6.5) +
            poly([(12, 4), (5, 15), (19, 15)], ACCENT, None) + poly([(60, 50), (49, 43), (49, 57)], ACCENT, None))


def g_parallel():
    return line((10, 44), (40, 10), ACCENT, 6.5) + line((24, 54), (54, 20), ACCENT, 6.5)


def g_perpendicular():
    return (line((32, 10), (32, 50), ACCENT, 6.5) + line((10, 52), (54, 52), ACCENT, 6.5) +
            path("M32,40 L42,40 L42,50", "none", INK, 2.2))


def g_tangent():
    return (circle((30, 36), 16, "none", ACCENT, 5.5) + line((6, 20), (58, 20), INK, 4.2) +
            circle((30, 20), 4.2, "#FFFFFF", INK, 2))


def g_equal():
    return line((10, 24), (54, 24), ACCENT, 7) + line((10, 42), (54, 42), ACCENT, 7)


def g_midpoint():
    return (line((6, 42), (58, 42), INK, 4) + poly([(32, 16), (44, 38), (20, 38)], ACCENT, INK, 2.2))


def g_concentric():
    return (circle((32, 32), 24, "none", ACCENT, 5) + circle((32, 32), 12, "none", ACCENT, 5) +
            circle((32, 32), 3.8, INK, None))


def g_fix():
    s = path("M21,30 L21,21 A11,11 0 0 1 43,21 L43,30", "none", INK, 5)
    s += '<rect x="13" y="28" width="38" height="28" rx="5" fill="%s" stroke="%s" stroke-width="2.2"/>' % (ACCENT, INK)
    s += circle((32, 39), 4, "#FFFFFF", None) + line((32, 41), (32, 48), "#FFFFFF", 3.4)
    return s


def g_symmetric():
    s = line((32, 4), (32, 60), INK, 2.4, 'stroke-dasharray="5,3.5"')
    s += path("M18,14 L8,14 L8,50 L18,50", "none", ACCENT, 5.5)
    s += path("M46,14 L56,14 L56,50 L46,50", "none", ACCENT, 5.5)
    return s


# --------------------------------------------------------------------------------------------
# Files

def new():
    return sheet(13, 7, 34, 44) + badge(46, 46, 11, "url(#bArrow)", "plus")


def folder_shape(open_=False):
    s = shadow(32, 54, 26, 3.5)
    s += path("M6,16 L6,51 L56,51 L56,20 L29,20 L24,14 L8,14 Z", "url(#yellowDark)", INK, 1.6)
    if open_:
        s += path("M6,51 L14,27 L62,27 L56,51 Z", "url(#yellow)", INK, 1.6)
    else:
        s += path("M6,24 L56,24 L56,51 L6,51 Z", "url(#yellow)", INK, 1.6)
        s += line((8, 26.5), (54, 26.5), "#FFF3C4", 1.1)
    return s


def open_():
    s = folder_shape(True)
    s += curved_arrow(40, 26, 13, 200, 300, 5, 8)
    return s


def save():
    s = shadow(32, 55, 24, 3.5)
    s += path("M8,10 L48,10 L56,18 L56,54 L8,54 Z", "url(#bArrow)", "#153F86", 1.6)
    s += '<rect x="16" y="10" width="28" height="15" rx="1.5" fill="url(#steel)" stroke="#153F86" stroke-width="1.3"/>'
    s += '<rect x="35" y="13" width="6" height="9" rx="1" fill="#2A3950"/>'
    s += '<rect x="14" y="33" width="36" height="21" rx="2" fill="url(#paper)" stroke="#153F86" stroke-width="1.3"/>'
    s += line((19, 40), (45, 40), "#9FB6D3", 1.4) + line((19, 46), (40, 46), "#9FB6D3", 1.4)
    return s


def export_stl():
    s = sheet(9, 6, 34, 44)
    # A triangle-mesh badge.
    tri = [(36, 32), (58, 32), (47, 54)]
    s += poly([(34, 32), (60, 32), (60, 58), (34, 58)], "url(#oTop)", INK, 1.5, 'rx="3"')
    s += line((34, 45), (60, 45), "#8A4608", 1.1) + line((47, 32), (47, 58), "#8A4608", 1.1)
    s += line((34, 32), (60, 58), "#8A4608", 1.1) + line((47, 32), (60, 45), "#8A4608", 1.1)
    s += line((34, 45), (47, 58), "#8A4608", 1.1)
    return s


def export_step():
    s = sheet(9, 6, 34, 44)
    iso = Iso(47, 48, 0.62)
    s += iso.box(-10, -10, 0, 20, 20, 16, top="url(#oTop)", left="url(#oSide)", right="url(#oDark)", sw=1.4)
    return s


def print3d():
    s = shadow(32, 58, 27, 3.5)
    # Frame.
    s += '<rect x="7" y="6" width="50" height="50" rx="4" fill="url(#dark)" stroke="%s" stroke-width="1.6"/>' % INK
    s += '<rect x="12" y="11" width="40" height="39" rx="2" fill="#E9F0F8"/>'
    # Gantry and nozzle.
    s += '<rect x="12" y="15" width="40" height="4" fill="url(#steel)" stroke="%s" stroke-width="1"/>' % INK
    s += poly([(26, 17), (38, 17), (38, 25), (34, 25), (32, 29), (30, 25), (26, 25)], "url(#steel)", INK, 1.3)
    # The part being printed on the bed.
    iso = Iso(32, 43, 0.5)
    s += iso.box(-12, -12, 0, 24, 24, 10, top="url(#oTop)", left="url(#oSide)", right="url(#oDark)", sw=1.3,
                 highlight=False)
    s += '<rect x="10" y="46" width="44" height="4" rx="1" fill="url(#steel)" stroke="%s" stroke-width="1.1"/>' % INK
    return s


def undo():
    return curved_arrow(32, 38, 17, 10, -176, 8.5, 12.5)


def redo():
    return curved_arrow(32, 38, 17, 170, 356, 8.5, 12.5)


# --------------------------------------------------------------------------------------------
# View and navigation

def home():
    s = shadow(32, 56, 24, 3.5)
    s += path("M14,30 L14,53 L50,53 L50,30", "url(#paper)", INK, 1.8)
    s += '<rect x="27" y="38" width="10" height="15" rx="1" fill="url(#bArrow)" stroke="#153F86" stroke-width="1.3"/>'
    s += path("M6,33 L32,9 L58,33 L52,33 L32,15 L12,33 Z", "url(#red)", "#8E1E0E", 1.6)
    return s


def orbit():
    s = shadow(32, 55, 18, 3)
    s += circle((32, 32), 14, "url(#sphere)", INK, 1.6)
    s += curved_arrow(32, 32, 24, 150, 390, 5, 9)
    return s


def pan():
    s = ""
    base = [(32, 3), (42, 15), (35.8, 15), (35.8, 25), (28.2, 25), (28.2, 15), (22, 15)]
    for ang in (0, 90, 180, 270):
        a = math.radians(ang)
        pts_ = [(32 + (x - 32) * math.cos(a) - (y - 32) * math.sin(a), 32 + (x - 32) * math.sin(a) + (y - 32) * math.cos(a))
                for x, y in base]
        s += arrow_poly(pts_, "url(#bArrow)" if ang in (0, 180) else "url(#bArrowH)")
    s += circle((32, 32), 6, "url(#steel)", INK, 1.5)
    return s


def zoom():
    s = shadow(40, 57, 18, 3)
    s += line((39, 39), (55, 55), INK, 9.5) + line((39, 39), (55, 55), "url(#dark)", 6.5)
    s += circle((26, 26), 17, "url(#glass)", INK, 2.4)
    s += circle((26, 26), 17, "none", "url(#steel)", 1.2)
    s += path("M16,22 A11,11 0 0 1 24,14.5", "none", "#FFFFFF", 2.6)
    s += line((20, 26), (32, 26), ACCENT_AXIS, 3) + line((26, 20), (26, 32), ACCENT_AXIS, 3)
    return s


def fit():
    iso = Iso(32, 36, 0.8)
    s = iso.floor_shadow(-11, -11, 22, 22)
    s += iso.box(-11, -11, 0, 22, 22, 16)
    for (x, y, dx, dy) in [(6, 6, 1, 1), (58, 6, -1, 1), (6, 58, 1, -1), (58, 58, -1, -1)]:
        s += path(f"M{x},{y + dy * 13} L{x},{y} L{x + dx * 13},{y}", "none", ACCENT_AXIS, 4.2)
    return s


def display():
    iso = Iso(32, 38, 1.05)
    P = iso.p
    x0, y0, d = -12, -12, 24
    s = iso.floor_shadow(x0, y0, d, d)
    s += iso.box(x0, y0, 0, d, d, d)
    # Right half shown as wireframe: overlay light fill and hidden edges.
    s += poly([P(x0 + d, y0, 0), P(x0 + d, y0 + d, 0), P(x0 + d, y0 + d, d), P(x0 + d, y0, d)], "#F4F8FD", INK, 1.6)
    s += line(P(x0 + d, y0, 0), P(x0, y0, 0), "#7F93AD", 1.2, 'stroke-dasharray="2.4,2"')
    s += line(P(x0, y0, 0), P(x0, y0 + d, 0), "#7F93AD", 1.2, 'stroke-dasharray="2.4,2"')
    s += line(P(x0, y0, 0), P(x0, y0, d), "#7F93AD", 1.2, 'stroke-dasharray="2.4,2"')
    return s


def grid():
    iso = Iso(32, 34, 1.0)
    P = iso.p
    s = iso.floor_shadow(-18, -18, 36, 36, 0.9)
    s += poly([P(-18, -18), P(18, -18), P(18, 18), P(-18, 18)], "url(#paper)", INK, 1.6)
    for i in range(1, 6):
        t = -18 + 36 * i / 6
        s += line(P(t, -18), P(t, 18), "#8FA8C9", 1.0) + line(P(-18, t), P(18, t), "#8FA8C9", 1.0)
    s += line(P(-18, 0), P(18, 0), "#E0442A", 1.6) + line(P(0, -18), P(0, 18), "#2F9B3F", 1.6)
    return s


def camera():
    s = shadow(32, 56, 25, 3.5)
    s += path("M8,20 L22,20 L26,13 L38,13 L42,20 L56,20 L56,51 L8,51 Z", "url(#dark)", INK, 1.6)
    s += line((10, 22.5), (54, 22.5), "#8698B0", 1.0)
    s += circle((32, 35), 12, "url(#steel)", INK, 1.6)
    s += circle((32, 35), 8, "url(#iris)", INK, 1.4)
    s += circle((29, 32), 2.4, "#FFFFFF", None, 0, 'fill-opacity="0.85"')
    s += '<rect x="45" y="24" width="7" height="4" rx="1" fill="#FFD58F"/>'
    return s


def origin():
    iso = Iso(28, 38, 1.0)
    P = iso.p
    s = shadow(30, 52, 18, 3.5)
    o = P(0, 0, 0)
    for end, col, dark in [(P(26, 0, 0), "#E5463A", "#9A1E14"), (P(0, 26, 0), "#3DAE4F", "#1B6A2A"),
                           (P(0, 0, 26), "#2F7BE0", "#153F86")]:
        s += line(o, end, dark, 5.2) + line(o, end, col, 3.2)
    s += circle(o, 5.5, "url(#sphere)", INK, 1.5)
    return s


# --------------------------------------------------------------------------------------------
# Browser

def eye_shape(open_=True):
    s = ""
    almond = "M4,32 Q32,8 60,32 Q32,56 4,32 Z"
    if open_:
        s += path(almond, "#FFFFFF", INK, 2.6)
        s += path("M9,32 Q32,16 55,32", "none", "#D5E3F3", 3, 'stroke-opacity="0.9"')
        s += circle((32, 32), 12.5, "url(#iris)", "#153F86", 1.6)
        s += circle((32, 32), 5.5, "#0E1B2C", None)
        s += circle((27.5, 27.5), 3.2, "#FFFFFF", None, 0, 'fill-opacity="0.95"')
        s += path(almond, "none", INK, 2.8)
    else:
        s += path("M4,32 Q32,46 60,32", "none", "#8A99AD", 4)
        for x0, x1 in [(14, 11), (24, 22.5), (40, 41.5), (50, 53)]:
            s += line((x0, 38.8 if x0 in (14, 50) else 41), (x1, 46 if x0 in (14, 50) else 48.5), "#8A99AD", 3)
    return s


def eye():
    return eye_shape(True)


def eye_off():
    s = path("M4,30 Q32,50 60,30", "none", "#56657B", 4.6)
    for (x0, y0, x1, y1) in [(13, 36.5, 9, 44), (24, 40, 22, 48.5), (40, 40, 42, 48.5), (51, 36.5, 55, 44)]:
        s += line((x0, y0), (x1, y1), "#56657B", 3.8)
    return s


def body():
    iso = Iso(32, 34, 1.2)
    s = iso.floor_shadow(-12, -12, 24, 24)
    s += iso.box(-12, -12, 0, 24, 24, 20, sw=2.2)
    return s


def sketch_node():
    iso = Iso(32, 34, 1.15)
    s = iso.floor_shadow(-16, -16, 32, 32, 1.0)
    s += iso_sheet(iso, -17, -17, 34, 34)
    P = iso.p
    s += path(f"M{f(P(-10, 8)[0])},{f(P(-10, 8)[1])} Q{f(P(0, -12)[0])},{f(P(0, -12)[1])} "
              f"{f(P(10, 6)[0])},{f(P(10, 6)[1])}", "none", ACCENT_AXIS, 4.2)
    return s


def plane_node():
    iso = Iso(32, 34, 1.2)
    P = iso.p
    s = iso.floor_shadow(-16, -16, 32, 32, 0.8)
    s += poly([P(-17, -17, 6), P(17, -17, 6), P(17, 17, 6), P(-17, 17, 6)], "url(#planeFill)", "#C8620C", 2.2)
    return s


def folder():
    return folder_shape(False)


def warning():
    s = shadow(32, 57, 24, 3)
    s += path("M32,6 L59,53 Q60,56 57,56 L7,56 Q4,56 5,53 Z", "url(#yellow)", "#9A6A00", 2)
    s += path("M29.4,22 L34.6,22 L33.6,40 L30.4,40 Z", "#3A2A00", None)
    s += circle((32, 47), 3.2, "#3A2A00", None)
    return s


def error():
    s = shadow(32, 58, 22, 3)
    s += circle((32, 31), 25, "url(#red)", "#8E1E0E", 2)
    s += path("M14,24 A20,20 0 0 1 44,11", "none", "#FFFFFF", 2.4, 'stroke-opacity="0.5"')
    s += line((22, 21), (42, 41), "#FFFFFF", 6) + line((42, 21), (22, 41), "#FFFFFF", 6)
    return s


# --------------------------------------------------------------------------------------------
# Timeline transport

def tri(x, y, w, h, left):
    return [(x, y + h / 2), (x + w, y), (x + w, y + h)] if left else [(x + w, y + h / 2), (x, y), (x, y + h)]


def t_first():
    return ('<rect x="9" y="14" width="7" height="36" rx="1.5" fill="url(#dark)" stroke="%s" stroke-width="1.2"/>' % INK +
            poly(tri(17, 14, 20, 36, True), "url(#dark)", INK, 1.2) + poly(tri(35, 14, 20, 36, True), "url(#dark)",
                                                                             INK, 1.2))


def t_back():
    return poly(tri(14, 12, 32, 40, True), "url(#dark)", INK, 1.4)


def t_forward():
    return poly(tri(18, 12, 32, 40, False), "url(#dark)", INK, 1.4)


def t_last():
    return (poly(tri(9, 14, 20, 36, False), "url(#dark)", INK, 1.2) + poly(tri(27, 14, 20, 36, False), "url(#dark)",
                                                                             INK, 1.2) +
            '<rect x="48" y="14" width="7" height="36" rx="1.5" fill="url(#dark)" stroke="%s" stroke-width="1.2"/>' % INK)


# --------------------------------------------------------------------------------------------
# Other commands

def settings():
    s = shadow(32, 58, 20, 3)
    teeth = 8
    ptsl = []
    for i in range(teeth * 4):
        a = 2 * math.pi * i / (teeth * 4) - math.pi / 2
        r = 26 if (i % 4) in (0, 1) else 19.5
        # Offset so each tooth is centred.
        a += math.pi / (teeth * 4)
        ptsl.append((32 + r * math.cos(a), 32 + r * math.sin(a)))
    s += poly(ptsl, "url(#steel)", INK, 1.8)
    s += circle((32, 32), 13, "url(#dark)", INK, 1.4)
    s += circle((32, 32), 6.5, "#F4F8FD", INK, 1.6)
    return s


def mcp_server():
    s = shadow(32, 58, 20, 3)
    s += line((32, 44), (32, 58), INK, 5) + line((32, 44), (32, 58), "url(#dark)", 3)
    s += path("M16,22 L48,22 L48,34 Q48,46 32,46 Q16,46 16,34 Z", "url(#bArrow)", "#153F86", 1.8)
    s += '<rect x="20" y="8" width="6" height="15" rx="2" fill="url(#steel)" stroke="%s" stroke-width="1.4"/>' % INK
    s += '<rect x="38" y="8" width="6" height="15" rx="2" fill="url(#steel)" stroke="%s" stroke-width="1.4"/>' % INK
    s += circle((32, 33), 3.2, "#9FF0AE", "#1E9A3A", 1.2)
    return s


def delete():
    s = shadow(32, 58, 18, 3)
    s += path("M14,18 L50,18 L46,56 L18,56 Z", "url(#steel)", INK, 1.8)
    for x in (24, 32, 40):
        s += line((x, 24), (x - (x - 32) * 0.1, 50), "#6F8098", 2)
    s += '<rect x="9" y="11" width="46" height="7" rx="2" fill="url(#red)" stroke="#8E1E0E" stroke-width="1.6"/>'
    s += '<rect x="25" y="6" width="14" height="6" rx="2" fill="url(#red)" stroke="#8E1E0E" stroke-width="1.4"/>'
    return s


def repeat():
    return curved_arrow(32, 32, 19, -60, 250, 7, 11)


ICONS = {
    "home": home, "orbit": orbit, "pan": pan, "zoom": zoom, "fit": fit, "display": display, "grid": grid,
    "camera": camera,
    "sketch": sketch, "finish-sketch": finish_sketch, "line": line_tool, "rectangle": rectangle,
    "center-rectangle": center_rectangle, "circle": circle_tool, "arc": arc, "point": point, "dimension": dimension,
    "construction": construction, "look-at": look_at,
    "coincident": g_coincident, "horizontal": g_horizontal, "vertical": g_vertical, "horizontal-vertical": g_hv,
    "parallel": g_parallel, "perpendicular": g_perpendicular, "tangent": g_tangent, "equal": g_equal,
    "midpoint": g_midpoint, "concentric": g_concentric, "fix": g_fix, "symmetric": g_symmetric,
    "extrude": extrude, "fillet": fillet, "chamfer": chamfer, "hole": hole, "combine": combine, "plane": plane,
    "section": section, "measure": measure, "overhang": overhang, "split": split, "move": move_tool, "offset": offset, "select": select_tool, "draft": draft, "mirror": mirror, "pattern-rect": pattern_rect, "pattern-circ": pattern_circ, "thread": thread,
    "undo": undo, "redo": redo, "save": save, "open": open_, "new": new, "export-stl": export_stl,
    "export-step": export_step, "print-3d": print3d,
    "eye": eye, "eye-off": eye_off, "body": body, "sketch-node": sketch_node, "plane-node": plane_node,
    "folder": folder, "warning": warning, "error": error,
    "timeline-first": t_first, "timeline-back": t_back, "timeline-forward": t_forward, "timeline-last": t_last,
    "origin": origin, "flip": flip,
    "settings": settings, "mcp-server": mcp_server, "delete": delete, "repeat": repeat,
}


MARGIN = 2.0


def fit(body_):
    """Scales and centres the icon so its drawing fills the artboard."""
    x0, y0, x1, y1 = BBOX
    if x1 <= x0 or y1 <= y0:
        return body_
    k = min((64 - 2 * MARGIN) / (x1 - x0), (64 - 2 * MARGIN) / (y1 - y0))
    tx = 32 - k * (x0 + x1) / 2
    ty = 32 - k * (y0 + y1) / 2
    return f'<g transform="translate({f(tx)},{f(ty)}) scale({k:.4f})">{body_}</g>'


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    for name, fn in ICONS.items():
        if only and name not in only:
            continue
        BBOX[:] = [1e9, 1e9, -1e9, -1e9]
        body_ = fn()
        for m in re.finditer(r'<rect x="([-\d.]+)" y="([-\d.]+)" width="([-\d.]+)" height="([-\d.]+)"'
                             r'(?:[^>]*stroke-width="([-\d.]+)")?', body_):
            x, y, w, h = (float(v) for v in m.groups()[:4])
            pad = float(m.group(5)) / 2 if m.group(5) else 0
            extent(x, y, pad)
            extent(x + w, y + h, pad)
        body_ = fit(body_)
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">'
               f"<defs>{DEFS.strip()}</defs>{body_}</svg>\n")
        with open(os.path.join(OUT, name + ".svg"), "w") as fh:
            fh.write(svg)
    print(f"wrote {len(ICONS) if not only else len(only)} icons to {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
