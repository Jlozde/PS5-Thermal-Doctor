#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the PS5 Thermal Doctor logo / PS5 Thermal Doctor logosunu çizer.

A fan (the cooling) with a pulse line across it (the check-up), in the colours
of the web UI. Writes:
  assets/logo.svg     rounded tile, for the README and the web UI
  assets/icon0.svg    full-bleed square, the source of the PS5 home-screen icon

assets/icon0.png (512 x 512, what the payload embeds) is rendered from
assets/icon0.svg with headless Chrome or Edge:
  python tools/make_logo.py --png

Fan (soğutma) ve üzerinden geçen nabız çizgisi (muayene), web arayüzünün
renkleriyle. --png, assets/icon0.png dosyasını headless Chrome/Edge ile üretir.
"""
import math, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

BG_TOP, BG_BOTTOM = "#1b2638", "#0e1116"
RING, BLADE_A, BLADE_B, HUB = "#2c3b52", "#7cc0ff", "#3b82d6", "#0e1116"
PULSE, PULSE_EDGE = "#2ecc71", "#0e1116"


def blade(angle):
    # One swept blade pointing up, rotated around the centre.
    d = ("M 0,-46 C 34,-70 74,-118 52,-168 "
         "C 26,-176 -8,-170 -26,-150 "
         "C -6,-120 -14,-78 -30,-50 Z")
    return f'<path d="{d}" transform="rotate({angle:.1f})" fill="url(#blade)"/>'


def svg(rounded):
    rx = 104 if rounded else 0
    blades = "\n      ".join(blade(i * 360 / 7) for i in range(7))
    # Pulse: flat, a small bump, the tall spike, a dip, flat again.
    pts = [(28, 300), (150, 300), (176, 270), (198, 300), (222, 300),
           (252, 120), (290, 410), (318, 262), (336, 300), (484, 300)]
    pulse = " ".join(f"{x},{y}" for x, y in pts)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <title>PS5 Thermal Doctor</title>
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{BG_TOP}"/>
      <stop offset="1" stop-color="{BG_BOTTOM}"/>
    </linearGradient>
    <linearGradient id="blade" x1="0" y1="-1" x2="0" y2="0" gradientUnits="objectBoundingBox">
      <stop offset="0" stop-color="{BLADE_A}"/>
      <stop offset="1" stop-color="{BLADE_B}"/>
    </linearGradient>
  </defs>
  <rect width="512" height="512" rx="{rx}" fill="url(#bg)"/>
  <circle cx="256" cy="256" r="196" fill="none" stroke="{RING}" stroke-width="16"/>
  <g transform="translate(256 256)">
      {blades}
    <circle r="44" fill="{HUB}" stroke="{RING}" stroke-width="8"/>
  </g>
  <polyline points="{pulse}" fill="none" stroke="{PULSE_EDGE}" stroke-width="46"
            stroke-linecap="round" stroke-linejoin="round"/>
  <polyline points="{pulse}" fill="none" stroke="{PULSE}" stroke-width="26"
            stroke-linecap="round" stroke-linejoin="round"/>
</svg>
'''


def find_browser():
    for p in (r"C:\Program Files\Google\Chrome\Application\chrome.exe",
              r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
              r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              shutil.which("google-chrome") or "", shutil.which("chromium") or ""):
        if p and os.path.exists(p):
            return p
    return None


def render_png(src_svg, dst_png):
    browser = find_browser()
    if not browser:
        print("Chrome / Edge not found / bulunamadı", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "icon.html")
        with open(src_svg, encoding="utf-8") as f:
            body = f.read()
        with open(page, "w", encoding="utf-8") as f:
            f.write("<!doctype html><html><body style='margin:0;background:#0e1116'>" + body + "</body></html>")
        out = os.path.join(tmp, "shot.png")
        subprocess.run([browser, "--headless=new", "--hide-scrollbars", "--force-device-scale-factor=1",
                        "--screenshot=" + out, "--window-size=512,512", "--user-data-dir=" + os.path.join(tmp, "p"),
                        "file:///" + page.replace("\\", "/")], check=True, capture_output=True, timeout=120)
        try:
            from PIL import Image
            Image.open(out).convert("RGB").crop((0, 0, 512, 512)).save(dst_png, optimize=True)
        except ImportError:
            shutil.copyfile(out, dst_png)
    print("written / yazıldı:", os.path.relpath(dst_png, ROOT))
    return 0


def main():
    os.makedirs(ASSETS, exist_ok=True)
    for name, rounded in (("logo.svg", True), ("icon0.svg", False)):
        with open(os.path.join(ASSETS, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(svg(rounded))
        print("written / yazıldı: assets/" + name)
    if "--png" in sys.argv:
        return render_png(os.path.join(ASSETS, "icon0.svg"), os.path.join(ASSETS, "icon0.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
