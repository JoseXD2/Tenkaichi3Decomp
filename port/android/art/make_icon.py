#!/usr/bin/env python3
"""Draws the icons from the Dragon Rage banner's four-star ball (port/android/art):
  icon.png          512 x 512  the app's icon (rounded dark tile)
  discord_icon.png  1024 x 1024 the Discord status image (asset key "icon"; Discord rounds it itself)
Usage: make_icon.py <out folder>"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

SS = 2  # supersampling


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def star(cx, cy, r, rot=-90.0):
    pts = []
    for i in range(10):
        rr = r if i % 2 == 0 else r * 0.42
        a = math.radians(rot + i * 36)
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    return pts


def draw(S, tile):
    W = S * SS
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    # background: the banner's night blue going to a warm brown at the top right
    bg = Image.new("RGBA", (W, W))
    px = bg.load()
    for y in range(0, W, 2):
        for x in range(0, W, 2):
            t = (x / W * 0.6 + (1 - y / W) * 0.4)
            c = lerp((10, 14, 34), (48, 30, 22), max(0.0, t - 0.25) / 0.75)
            for dx in (0, 1):
                for dy in (0, 1):
                    px[x + dx, y + dy] = c + (255,)
    img.alpha_composite(bg)
    cx, cy, R = W * 0.5, W * 0.5, W * 0.36
    # speed lines from the centre
    lines = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ld = ImageDraw.Draw(lines)
    for i in range(46):
        a = i * 2.39996
        r0 = R * (1.05 + 0.5 * ((i * 37) % 11) / 11)
        r1 = r0 + W * (0.12 + 0.18 * ((i * 53) % 7) / 7)
        col = (255, 214, 140, 70) if i % 3 else (150, 170, 255, 55)
        ld.line([cx + math.cos(a) * r0, cy + math.sin(a) * r0, cx + math.cos(a) * r1, cy + math.sin(a) * r1], fill=col, width=max(1, W // 400))
    img.alpha_composite(lines.filter(ImageFilter.GaussianBlur(W / 900)))
    # the glow around the ball
    glow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([cx - R * 1.15, cy - R * 1.15, cx + R * 1.15, cy + R * 1.15], fill=(255, 180, 70, 90))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(W / 28)))
    # the ball: a radial gradient lit from the top left
    ball = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    bp = ball.load()
    lx, ly = cx - R * 0.38, cy - R * 0.42
    x0, x1, y0, y1 = int(cx - R) - 2, int(cx + R) + 2, int(cy - R) - 2, int(cy + R) + 2
    for y in range(y0, y1):
        for x in range(x0, x1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= R * R:
                t = min(1.0, math.hypot(x - lx, y - ly) / (R * 1.55))
                if t < 0.45:
                    c = lerp((255, 196, 84), (250, 156, 34), t / 0.45)
                else:
                    c = lerp((250, 156, 34), (214, 90, 10), (t - 0.45) / 0.55)
                bp[x, y] = c + (255,)
    # stars: two by two, a little up and left like the banner's
    sd = ImageDraw.Draw(ball)
    sr = R * 0.25
    for gx, gy in ((-0.3, -0.27), (0.27, -0.27), (-0.3, 0.27), (0.27, 0.27)):
        sx, sy = cx + gx * R - R * 0.04, cy + gy * R - R * 0.04
        sd.polygon(star(sx + R * 0.012, sy + R * 0.015, sr), fill=(150, 30, 15, 120))
        sd.polygon(star(sx, sy, sr), fill=(222, 38, 28, 255))
    # rim shade
    rim = Image.new("L", (W, W), 0)
    ImageDraw.Draw(rim).ellipse([cx - R, cy - R, cx + R, cy + R], fill=255)
    inner = Image.new("L", (W, W), 0)
    ImageDraw.Draw(inner).ellipse([cx - R * 0.9, cy - R * 0.92, cx + R * 0.94, cy + R * 0.9], fill=255)
    inner = inner.filter(ImageFilter.GaussianBlur(R * 0.08))
    edge = ImageChops.subtract(rim, inner)
    shade = Image.new("RGBA", (W, W), (120, 40, 0, 0))
    shade.putalpha(edge.point(lambda v: v * 90 // 255))
    ball.alpha_composite(shade)
    # gloss: the soft white spot of the banner's ball
    gl = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(gl).ellipse([cx - R * 0.66, cy - R * 0.72, cx - R * 0.2, cy - R * 0.46], fill=(255, 250, 225, 235))
    gl = gl.filter(ImageFilter.GaussianBlur(R * 0.035))
    ball.alpha_composite(gl)
    ball.putalpha(ImageChops.multiply(ball.getchannel("A"), rim))
    img.alpha_composite(ball)
    if tile:  # a rounded tile, as launchers without adaptive icons show it
        m = Image.new("L", (W, W), 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, W - 1, W - 1], radius=W * 0.22, fill=255)
        img.putalpha(ImageChops.multiply(img.getchannel("A"), m))
    return img.resize((S, S), Image.LANCZOS)


out = sys.argv[1] if len(sys.argv) > 1 else "."
draw(512, True).save(f"{out}/icon.png")
draw(1024, False).save(f"{out}/discord_icon.png")
