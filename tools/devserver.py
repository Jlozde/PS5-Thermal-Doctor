#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fake PS5 server for trying the web UI on a PC.
PC'de web arayüzünü denemek için sahte PS5 sunucusu.

    python tools/devserver.py                  # healthy console / sağlıklı konsol
    PS5TD_SIM=dusty python tools/devserver.py  # dusty console / tozlu konsol
    http://localhost:8088

It serves the same API shape as the payload (including ?lang=) and uses a
simplified copy of the diagnosis logic; the real logic lives in src/analysis.c.
Texts come from tools/i18n_texts.py, the same table the payload is built from.
Payload ile aynı API şeklini (?lang= dahil) sunar; teşhis mantığının
basitleştirilmiş bir kopyasını kullanır, gerçeği src/analysis.c içindedir.
Metinler, payload'ın da kullandığı tools/i18n_texts.py tablosundan gelir.

Environment / ortam: PS5TD_SIM=healthy|dusty, PS5TD_PORT=8088,
PS5TD_BACKFILL=<seconds of fake history>, PS5TD_BIND=127.0.0.1,
PS5TD_LANG=en|tr|de|fr|es|ru|ko|zh (default "console" language).
"""
import json, math, os, random, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i18n_texts import LANGS, SERVER  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODE = os.environ.get("PS5TD_SIM", "healthy")
PORT = int(os.environ.get("PS5TD_PORT", "8088"))
CODES = [c for c, _, _ in LANGS]
CONSOLE_LANG = os.environ.get("PS5TD_LANG", "en").lower()[:2]
CONSOLE_LANG = CONSOLE_LANG if CONSOLE_LANG in CODES else "en"
T0 = time.time()
random.seed(7)

CFG = dict(lang="auto", temp_unit="c", http_port=PORT, sample_ms=1000, csv_every_s=5, fps_enabled=1, escalate=1,
           notify_startup=1, notify_session=1, notify_live_warn=1, home_tile=1, warmup_s=600, min_ss_s=300, min_session_s=180,
           k_fan=0.30, k_pow=0.25, watch_drift=4.0, clean_drift=8.0, service_drift=14.0,
           abs_watch_fan=60.0, abs_watch_soc=76.0, abs_clean_fan=70.0, abs_clean_soc=80.0,
           abs_service_fan=85.0, abs_service_soc=86.0, hot_soc_c=95.0, baseline_sessions=5,
           recent_sessions=3, keep_sessions=60)
# Same limits as config_sanitize() in src/config.c / src/config.c içindeki config_sanitize() ile aynı sınırlar
LIMITS = dict(http_port=(1024, 65535), sample_ms=(250, 10000), csv_every_s=(1, 600), warmup_s=(60, 3600),
              min_ss_s=(60, 3600), min_session_s=(30, 3600), k_fan=(0, 2), k_pow=(0, 2), watch_drift=(0.5, 50),
              clean_drift=(0.5, 60), service_drift=(0.5, 80), abs_watch_fan=(0, 100), abs_clean_fan=(0, 100),
              abs_service_fan=(0, 100), abs_watch_soc=(30, 130), abs_clean_soc=(30, 130), abs_service_soc=(30, 130),
              hot_soc_c=(60, 130), baseline_sessions=(1, 50), recent_sessions=(1, 20), keep_sessions=(5, 500))
BOOLS = ("fps_enabled", "escalate", "notify_startup", "notify_session", "notify_live_warn", "home_tile")
EPOCH = 0
LOG = []  # (time, en, tr)


def L(lang, en, tr):
    """Technical text (log, diagnostics): English / Turkish, others fall back to English."""
    return tr if lang == "tr" else en


def X(key, lang, *args):
    """Player-facing text from tools/i18n_texts.py, printf-style like the payload."""
    fmt = SERVER[key][CODES.index(lang)]
    return fmt % args if args else fmt


def console_lang():
    return CFG["lang"] if CFG["lang"] in CODES else CONSOLE_LANG


def log(en, tr):
    LOG.append((time.strftime("%Y-%m-%d %H:%M:%S "), en, tr))
    del LOG[:-128]
    print(time.strftime("%H:%M:%S ") + L(console_lang(), en, tr), flush=True)


def sanitize():
    for k, (lo, hi) in LIMITS.items():
        CFG[k] = type(CFG[k])(min(max(CFG[k], lo), hi))
    CFG["clean_drift"] = max(CFG["clean_drift"], CFG["watch_drift"])
    CFG["service_drift"] = max(CFG["service_drift"], CFG["clean_drift"])
    for k in BOOLS:
        CFG[k] = 1 if CFG[k] else 0


def parse_unit(v):
    v = v.strip().lower()
    return "c" if v in ("c", "celsius") else "f" if v in ("f", "fahrenheit") else None


# Values stay in °C; texts follow temp_unit / Değerler °C kalır, metinler temp_unit'i izler
def tc(v): return v * 1.8 + 32 if CFG["temp_unit"] == "f" else v
def td(v): return v * 1.8 if CFG["temp_unit"] == "f" else v
def tsym(): return "°F" if CFG["temp_unit"] == "f" else "°C"


def cdi(soc, fan, pw):
    v = soc + CFG["k_fan"] * (fan - 40)
    if pw is not None:
        v -= CFG["k_pow"] * (pw - 150)
    return v


def verdict_abs(soc, fan):
    if fan >= CFG["abs_service_fan"] and soc >= CFG["abs_service_soc"]: return 3
    if fan >= CFG["abs_clean_fan"] and soc >= CFG["abs_clean_soc"]: return 2
    if fan >= CFG["abs_watch_fan"] and soc >= CFG["abs_watch_soc"]: return 1
    return 0


def abs_reason(lang, soc, fan, pw):
    """Same wording as an_verdict_abs() in src/analysis.c."""
    v = verdict_abs(soc, fan)
    if v > 0:
        return X(("abs_watch", "abs_clean", "abs_service")[v - 1], lang, fan, tc(soc), tsym())
    return X("ok_power", lang, tc(soc), tsym(), fan, pw)


NAMES = {-1: "NO_DATA", 0: "OK", 1: "WATCH", 2: "CLEAN", 3: "SERVICE"}
VKEY = {-1: "na", 0: "ok", 1: "watch", 2: "clean", 3: "service"}

# ---- history: 60 days, 14 sessions / geçmiş: 60 gün, 14 oturum ----
HIST = []
now_ms = int(time.time() * 1000)
for i in range(14):
    days_ago = 60 - i * 4.2
    drift = (i * 1.1) if MODE == "dusty" else random.uniform(-0.8, 0.8)
    soc = 71 + drift + random.uniform(-1.5, 1.5)
    fan = 38 + (drift * 2.5 if MODE == "dusty" else 0) + random.uniform(-3, 3)
    pw = random.uniform(140, 185)
    dur = int(random.uniform(25, 110) * 60)
    s = dict(start_ms=int(now_ms - days_ago * 86400000), epoch=0, title=random.choice(["PPSA01234", "PPSA05678", "CUSA03173"]),
             dur_s=dur, ss_valid=True, ss_samples=dur - 600, ss_soc=round(soc, 1), ss_cpu=round(soc + 6, 1),
             ss_fan=round(fan, 1), ss_power=round(pw, 1), ss_fps=round(random.choice([30, 60, 60, 60, 120]) - random.uniform(0, 3), 1),
             max_soc=round(soc + 5, 1), max_cpu=round(soc + 11, 1), max_fan=round(fan + 12, 1),
             warm_rate=round(random.uniform(3, 6), 2), hot_share=0.0, danger_s=0)
    s["cdi"] = round(cdi(s["ss_soc"], s["ss_fan"], s["ss_power"]), 2)
    s["verdict"] = verdict_abs(s["ss_soc"], s["ss_fan"])
    s["start"] = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(s["start_ms"] / 1000))
    s["verdict_name"] = NAMES[s["verdict"]]
    HIST.append(s)


def overall(lang):
    hs = [h for h in HIST if h["epoch"] == EPOCH and h["ss_valid"]]
    base = hs[:CFG["baseline_sessions"]]
    bv = len(base) == CFG["baseline_sessions"]
    mean = sum(h["cdi"] for h in base) / len(base) if base else 0
    sd = math.sqrt(sum((h["cdi"] - mean) ** 2 for h in base) / len(base)) if base else 0
    rest = hs[CFG["baseline_sessions"]:]
    recent = rest[-CFG["recent_sessions"]:] if rest else hs[-1:]
    rc = sum(h["cdi"] for h in recent) / len(recent) if recent else 0
    drift = rc - mean if bv else 0
    vrel = -1 if not bv else (3 if drift >= CFG["service_drift"] else 2 if drift >= CFG["clean_drift"] else 1 if drift >= CFG["watch_drift"] else 0)
    last = hs[-1] if hs else None
    vabs = last["verdict"] if last else -1
    v = max(vabs, vrel)
    trend_valid = len(hs) >= 6
    slope = 0.0
    if trend_valid:
        xs = [(h["start_ms"] - hs[0]["start_ms"]) / 86400000 for h in hs]; ys = [h["cdi"] for h in hs]
        n = len(xs); sx = sum(xs); sy = sum(ys); sxx = sum(x * x for x in xs); sxy = sum(x * y for x, y in zip(xs, ys))
        slope = (n * sxy - sx * sy) / (n * sxx - sx * sx) * 30
        if slope >= 2 and v < 1: v = 1
    if last:
        reason_abs = abs_reason(lang, last['ss_soc'], last['ss_fan'], last['ss_power'])
    else:
        reason_abs = X("no_session", lang, (CFG["warmup_s"] + CFG["min_ss_s"] + 59) // 60)
    if bv:
        reason_rel = X("rel", lang, td(drift), tsym(), tc(mean), td(sd), len(recent), tc(rc))
    else:
        reason_rel = X("building", lang, len(base), CFG["baseline_sessions"])
    if trend_valid:
        worse = X("trend_worse", lang) if slope >= 2 else ""
        trend = X("trend", lang, len(hs), td(slope), tsym(), worse)
    else:
        trend = X("trend_none", lang)
    k = VKEY[v]
    return dict(verdict=v, name=NAMES[v], badge=X("badge_" + k, lang), label=X("label_" + k, lang), advice=X("advice_" + k, lang),
                verdict_abs=vabs, reason_abs=reason_abs, verdict_rel=vrel, reason_rel=reason_rel, trend_text=trend,
                trend_valid=trend_valid, trend_per_30d=slope, drift=drift, recent_cdi=rc, recent_n=len(recent),
                baseline=dict(valid=bv, n=len(base), need=CFG["baseline_sessions"], cdi_mean=mean, cdi_sd=sd,
                              first_ms=base[0]["start_ms"] if base else 0, last_ms=base[-1]["start_ms"] if base else 0),
                last=last)


# ---- live simulation / canlı simülasyon ----
RING = []
SESSION_START = time.time()


def sim_sample(t):
    el = t - SESSION_START
    ss_soc = 88 if MODE == "dusty" else 72
    ss_fan = 90 if MODE == "dusty" else 38
    k = 1 - math.exp(-el / 240)
    soc = 45 + (ss_soc - 45) * k + random.uniform(-1, 1)
    fan = 22 + (ss_fan - 22) * k + random.uniform(-2, 2)
    pw = 160 + 15 * math.sin(el / 37) + random.uniform(-5, 5)
    return dict(wall_ms=int(t * 1000), game=True, cpu_live=True, cpu_c=round(soc + 6), soc_c=round(soc), gpu_c=round(soc - 2),
                fan_pct=round(fan, 1), fan_raw=round(fan * 10.24), power_w=round(pw, 1), gpu_w=round(pw * 0.55, 1),
                cpu_w=round(pw * 0.25, 1), mem_w=0.0, fps=round(60 - random.uniform(0, 2), 1),
                soc_raw=[round(soc - 5), round(soc - 1), round(soc - 2), round(soc), round(soc - 1), round(soc - 3), round(soc - 2), round(soc)])


def tick():
    t = time.time()
    if not RING or t - RING[-1]["wall_ms"] / 1000 >= 1:
        RING.append(sim_sample(t))
        del RING[:-7200]


def backfill(seconds):
    """Pretend the session started `seconds` ago so the charts are not empty."""
    global SESSION_START
    SESSION_START = time.time() - seconds
    for i in range(seconds, 0, -1):
        RING.append(sim_sample(time.time() - i))


def ticker():
    while True:
        tick()
        time.sleep(1)


def live_summary(lang):
    el = time.time() - SESSION_START
    ss = [s for s in RING if s["wall_ms"] / 1000 - SESSION_START >= CFG["warmup_s"]]
    valid = len(ss) >= CFG["min_ss_s"]
    soc = sum(s["soc_c"] for s in ss) / len(ss) if ss else -1
    fan = sum(s["fan_pct"] for s in ss) / len(ss) if ss else -1
    pw = sum(s["power_w"] for s in ss) / len(ss) if ss else -1
    v = verdict_abs(soc, fan) if valid else -1
    if valid:
        reason = abs_reason(lang, soc, fan, pw)
    else:
        need = (CFG["warmup_s"] + CFG["min_ss_s"] + 59) // 60
        reason = X("not_steady", lang, need)
    live = dict(start_ms=int(SESSION_START * 1000), start="", epoch=EPOCH, title="PPSA01234", dur_s=int(el), ss_valid=valid,
                ss_samples=len(ss), ss_soc=soc if valid else None, ss_cpu=soc + 6 if valid else None, ss_fan=fan if valid else None,
                ss_power=pw if valid else None, ss_fps=59.1 if valid else None, max_soc=max((s["soc_c"] for s in RING), default=None),
                max_cpu=None, max_fan=max((s["fan_pct"] for s in RING), default=None), warm_rate=4.2 if el > 300 else None,
                hot_share=0, danger_s=0, cdi=cdi(soc, fan, pw) if valid else None, verdict=v, verdict_name=NAMES[v])
    return live, v, reason


def query_lang(q):
    v = q.get("lang", [""])[0].lower()
    return v if v in CODES else console_lang()


class H(BaseHTTPRequestHandler):
    def _send(self, code, ctype, body):
        if isinstance(body, str): body = body.encode("utf-8")
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store"); self.end_headers(); self.wfile.write(body)

    def _json(self, obj): self._send(200, "application/json; charset=utf-8", json.dumps(obj))

    def log_message(self, *a): pass

    def do_GET(self):
        tick()
        u = urlparse(self.path); q = parse_qs(u.query)
        if u.path in ("/", "/index.html"):
            with open(os.path.join(ROOT, "web", "index.html"), "rb") as f:
                return self._send(200, "text/html; charset=utf-8", f.read())
        if u.path in ("/icon.png", "/favicon.ico"):
            with open(os.path.join(ROOT, "assets", "icon0.png"), "rb") as f:
                return self._send(200, "image/png", f.read())
        if u.path == "/api/status":
            lang = query_lang(q)
            live, lv, reason = live_summary(lang)
            ok = L(lang, "ok", "var")
            symbols = (f"CpuTemp:{ok} SocSensor:{ok} FanDuty:{ok} SocPower:{ok} BigApp:{ok} SysLang:{ok} "
                       + L(lang, f"(SIMULATION: {MODE})", f"(SİMÜLASYON: {MODE})"))
            return self._json(dict(app="ps5td", version="0.2.3-sim", pid=os.getpid(), fw="11.60", ip="127.0.0.1",
                                   uptime_s=int(time.time() - T0), tile="present" if CFG["home_tile"] else "off", tile_code=0, tile_port=PORT, tile_want=PORT, lang=lang, console_lang=console_lang(), config_lang=CFG["lang"], temp_unit=CFG["temp_unit"],
                                   symbols=symbols, dce=L(lang, "reading the flip counter", "flip sayacı okunuyor"),
                                   samples_total=len(RING), history_n=len(HIST), epoch=EPOCH, epoch_since_ms=0, now=RING[-1],
                                   session=dict(active=True, title="PPSA01234", dur_s=int(time.time() - SESSION_START), live=live,
                                                live_verdict=lv, live_reason=reason),
                                   overall=overall(lang)))
        if u.path == "/api/live":
            minutes = int(q.get("minutes", ["30"])[0]); step = max(1, int(q.get("step", ["5"])[0]))
            pts = RING[-minutes * 60:][::step]
            return self._json(dict(step=step, n=len(pts), t=[p["wall_ms"] for p in pts], cpu=[p["cpu_c"] for p in pts], soc=[p["soc_c"] for p in pts],
                                   gpu=[p["gpu_c"] for p in pts], fan=[p["fan_pct"] for p in pts], pw=[p["power_w"] for p in pts],
                                   fps=[p["fps"] for p in pts], game=[1] * len(pts)))
        if u.path == "/api/sessions":
            return self._json(dict(history=HIST, files=[]))
        if u.path == "/api/config":
            txt = "# PS5 Thermal Doctor (simulation / simülasyon)\n\n" + "".join(
                f"{k} = {v:.2f}\n" if isinstance(v, float) else f"{k} = {v}\n" for k, v in CFG.items())
            return self._send(200, "text/plain; charset=utf-8", txt)
        if u.path == "/api/log":
            lang = query_lang(q)
            return self._send(200, "text/plain; charset=utf-8", "".join(t + L(lang, en, tr) + "\n" for t, en, tr in LOG))
        self._send(404, "text/plain", "not found")

    def do_POST(self):
        global EPOCH
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length", "0")); body = self.rfile.read(n).decode("utf-8", "replace")
        if u.path == "/api/config":
            bad = 0
            for line in body.splitlines():
                line = line.strip()
                if not line or line[0] in "#;[": continue
                if "=" not in line: bad += 1; continue
                k, v = [x.strip() for x in line.split("=", 1)]
                if k == "lang":
                    if v.lower() in ["auto"] + CODES: CFG["lang"] = v.lower()
                    else: bad += 1
                elif k == "temp_unit":
                    if parse_unit(v): CFG["temp_unit"] = parse_unit(v)
                    else: bad += 1
                elif k in CFG:
                    try: CFG[k] = type(CFG[k])(float(v))
                    except ValueError: bad += 1
                else: bad += 1
            sanitize()
            log(f"Settings updated ({bad} bad lines).", f"Ayarlar güncellendi ({bad} hatalı satır).")
            return self._json(dict(ok=True, bad_lines=bad))
        if u.path == "/api/baseline/reset":
            EPOCH += 1
            log(f"New baseline started (counter {EPOCH}).", f"Yeni referans başlatıldı (sayaç {EPOCH}).")
            return self._json(dict(ok=True))
        if u.path == "/api/notify/test":
            log("Notification test (simulation).", "Bildirim testi (simülasyon).")
            return self._json(dict(ok=True))
        self._send(404, "text/plain", "not found")


if __name__ == "__main__":
    import sys, threading
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    backfill(int(os.environ.get("PS5TD_BACKFILL", "1500")))
    threading.Thread(target=ticker, daemon=True).start()
    log(f"devserver started, mode={MODE}", f"devserver başladı, mod={MODE}")
    print(f"PS5 Thermal Doctor devserver ({MODE}) -> http://localhost:{PORT}")
    ThreadingHTTPServer((os.environ.get("PS5TD_BIND", "127.0.0.1"), PORT), H).serve_forever()
