#!/usr/bin/env python3
"""Stand-in for the radar's web API, for working on data/portal.html.

    python3 sim/mock_device.py [port]      then open http://localhost:8642/

Pick the situation to fake with ?scene= on the page URL (it sticks until
changed): ok, empty, error, weak, approx, unset, offline, portal, portal-note.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

PAGE = Path(__file__).resolve().parent.parent / "data" / "portal.html"
START = time.time()
RANGES = [5, 10, 15, 25, 50, 100, 150]
state = {
    "scene": "ok",
    "scan_started": 0.0,
    "settings": {
        "lat": 52.3676, "lon": 4.9041, "loc_set": True, "loc_approx": False,
        "range": 4, "ranges_km": RANGES, "miles": False, "runways": True,
        "ground": False, "source": 0, "labels": 6, "labels_max": 12, "wifi_power": 0,
        "swap_rb": True,
    },
}
AIRCRAFT = [
    {"cs": "CCA174", "t": "B789", "alt": "5225 ft", "km": 21.8, "brg": 207, "gs": 246},
    {"cs": "QFA103", "t": "A333", "alt": "3050 ft", "km": 24.4, "brg": 171, "gs": 190},
    {"cs": "JST419", "t": "A320", "alt": "6025 ft", "km": 29.9, "brg": 164, "gs": 255},
    {"cs": "7c6b2f", "t": "", "alt": "", "km": 36.1, "brg": 301, "gs": 0},
    {"cs": "QFA75", "t": "B789", "alt": "21200 ft", "km": 61.5, "brg": 58, "gs": 430},
]


def status():
    scene = state["scene"]
    s = state["settings"]
    portal = scene.startswith("portal")
    online = scene not in ("offline",) and not portal
    radius = round(RANGES[s["range"]] * 4 / 3 * 118 / 107, 1)
    planes = [a for a in AIRCRAFT if a["km"] <= radius] if scene in ("ok", "approx", "unset") else []
    feed = {
        "attempted": not portal, "ok": True, "ever_ok": not portal, "http": 200,
        "error": "", "detail": "", "age_s": 2, "fetch_ms": 840, "bytes": 7333,
        "reported": 15, "shown": len(planes), "ground": 11, "nopos": 1,
        "nearest_km": planes[0]["km"] if planes else -1, "radius_km": radius,
        "source": "adsb.fi", "ok_count": 212, "fail_count": 3,
    }
    if scene == "error":
        feed.update(ok=False, ever_ok=False, http=-1, error="Connect failed",
                    detail="(-32512) SSL - Memory allocation failed", age_s=-1,
                    bytes=0, reported=0, ground=0, nopos=0, ok_count=0, fail_count=9)
    if scene == "weak":
        feed.update(ok=False, http=-1, error="Connect failed",
                    detail="ERROR - Generic error", age_s=150, fail_count=5)
        planes = []
    return {
        "fw": "2.0.1", "mode": "portal" if portal else "station", "online": online,
        "ssid": "HomeNet-5G" if online else "",
        "saved_ssid": "" if scene == "portal" else "HomeNet-5G",
        "ip": "192.168.1.137" if online else "", "rssi": (-83 if scene == "weak" else -61) if online else 0,
        "rssi_weak": -78, "tx_dbm": 8.5,
        "host": "plane-radar.local",
        "note": "Can't join HomeNet-5G" if scene == "portal-note" else "",
        "uptime_s": int(time.time() - START) + 4000, "heap": 151000,
        "heap_min": 96000, "heap_block": 88000,
        "loc": {"lat": s["lat"], "lon": s["lon"],
                "set": scene != "unset" and s["loc_set"],
                "approx": scene == "approx" or s["loc_approx"]},
        "feed": feed, "aircraft_total": len(planes), "aircraft": planes,
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        if url.path == "/":
            if "scene" in query:
                state["scene"] = query["scene"][0]
            self.reply(200, PAGE.read_bytes(), "text/html; charset=utf-8")
        elif url.path == "/api/status":
            self.reply(200, status())
        elif url.path == "/api/settings":
            s = dict(state["settings"])
            if state["scene"] == "unset":
                s["loc_set"] = False
            if state["scene"] == "approx":
                s["loc_approx"] = True
            self.reply(200, s)
        elif url.path == "/api/scan":
            if "start" in query:
                state["scan_started"] = time.time()
            if not state["scan_started"]:
                self.reply(200, {"state": "idle"})
            elif time.time() - state["scan_started"] < 2:
                self.reply(200, {"state": "scanning"})
            else:
                self.reply(200, {"state": "done", "networks": [
                    {"ssid": "HomeNet-5G", "rssi": -52, "open": False},
                    {"ssid": "Telstra8F21A0 with a rather long name", "rssi": -66, "open": False},
                    {"ssid": "CafeGuest", "rssi": -74, "open": True},
                    {"ssid": "NETGEAR_EXT", "rssi": -85, "open": False},
                ]})
        else:
            self.reply(404, {"ok": False, "error": "Not found"})

    def do_POST(self):
        url = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode(), keep_blank_values=True).items()}
        s = state["settings"]
        if url.path == "/api/settings":
            if "lat" in form or "lon" in form:
                try:
                    lat, lon = float(form.get("lat", "")), float(form.get("lon", ""))
                    assert -90 <= lat <= 90 and -180 <= lon <= 180
                except (ValueError, AssertionError):
                    return self.reply(400, {"ok": False, "error": "Latitude must be a number between -90 and 90."})
                s.update(lat=lat, lon=lon, loc_set=True, loc_approx=form.get("loc_approx") == "1")
                if state["scene"] in ("unset", "approx"):
                    state["scene"] = "ok"
            for key in ("range", "source", "labels", "wifi_power"):
                if key in form:
                    s[key] = int(form[key])
            for key in ("miles", "runways", "ground", "swap_rb"):
                if key in form:
                    s[key] = form[key] == "1"
            self.reply(200, dict(s, ok=True))
        elif url.path == "/api/wifi":
            if not form.get("ssid"):
                return self.reply(400, {"ok": False, "error": "Choose a network or type its name."})
            self.reply(200, {"ok": True})
        elif url.path == "/api/locate":
            time.sleep(0.8)
            self.reply(200, {"ok": True, "lat": 52.37, "lon": 4.89, "place": "Amsterdam, Netherlands"})
        elif url.path in ("/api/restart", "/api/factory-reset"):
            self.reply(200, {"ok": True})
        else:
            self.reply(404, {"ok": False, "error": "Not found"})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8642
    print(f"Mock radar on http://localhost:{port}/  (add ?scene=portal etc.)")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
