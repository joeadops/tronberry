#!/usr/bin/env python3
"""Bridge a Bambu Lab printer's local MQTT feed to a status.json file.

Connects to the printer over the LAN (no Bambu cloud account needed) and
serves http://<host>:<port>/status.json in the format expected by the
Tronbyt "Bambu Printer Status" app (bambuprinterstatus).

Configuration comes from environment variables:
  BAMBU_HOST         Printer IP address (required)
  BAMBU_SERIAL       Printer serial number (required)
  BAMBU_ACCESS_CODE  LAN access code from the printer screen (required)
  BAMBU_NAME         Display name (default: "Bambu Printer")
  BRIDGE_PORT        HTTP port to serve status.json on (default: 8787)
  BRIDGE_STATE_FILE  Where to persist last-completed job info
                     (default: ./bridge_state.json)
"""

import json
import logging
import os
import ssl
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import paho.mqtt.client as mqtt

HOST = os.environ["BAMBU_HOST"]
SERIAL = os.environ["BAMBU_SERIAL"]
ACCESS_CODE = os.environ["BAMBU_ACCESS_CODE"]
NAME = os.environ.get("BAMBU_NAME", "Bambu Printer")
PORT = int(os.environ.get("BRIDGE_PORT", "8787"))
STATE_FILE = Path(os.environ.get("BRIDGE_STATE_FILE", "bridge_state.json"))

# The printer pushes reports every second or so while connected. If nothing
# arrives for this long, report it as offline.
STALE_AFTER_SECS = 60
# P1/A1 printers only send changed fields, so periodically ask for a full
# report to recover from anything missed. Bambu advises against doing this
# often on P1-series printers.
PUSHALL_INTERVAL_SECS = 300

log = logging.getLogger("bambu-bridge")


def iso(ts):
    return datetime.fromtimestamp(ts, timezone.utc).replace(microsecond=0).isoformat()


def fmt_duration(seconds):
    if seconds is None:
        return None
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h" if minutes == 0 else f"{hours}h {minutes}m"


def normalize_state(gcode_state):
    state = (gcode_state or "").upper()
    if state == "RUNNING":
        return "PRINTING"
    if state == "PAUSE":
        return "PAUSED"
    if state in ("PREPARE", "SLICING"):
        return "PREPARING"
    if state == "FAILED":
        return "FAILED"
    return "IDLE"  # FINISH, IDLE, or unknown


class PrinterState:
    def __init__(self):
        self.lock = threading.Lock()
        self.report = {}  # merged "print" fields from MQTT
        self.last_message_at = 0.0
        self.prev_state = None
        self.job_started_at = None
        self.history = self._load_history()

    def _load_history(self):
        try:
            return json.loads(STATE_FILE.read_text())
        except (OSError, ValueError):
            return {}

    def _save_history(self):
        try:
            STATE_FILE.write_text(json.dumps(self.history, indent=2))
        except OSError as e:
            log.warning("Could not save %s: %s", STATE_FILE, e)

    def update(self, fields):
        with self.lock:
            self.report.update(fields)
            self.last_message_at = time.time()
            self._track_transitions()

    def _track_transitions(self):
        state = normalize_state(self.report.get("gcode_state"))
        if state == self.prev_state:
            return
        now = time.time()
        if state == "PRINTING" and self.prev_state != "PAUSED":
            self.job_started_at = now
            self.history.pop("idle_since", None)
        elif self.prev_state in ("PRINTING", "PAUSED") and state in ("IDLE", "FAILED"):
            self.history["idle_since"] = iso(now)
            self.history["last_completed_job_name"] = self.report.get("subtask_name") or None
            self.history["last_completed_end_time"] = iso(now)
            if self.job_started_at:
                duration = int(now - self.job_started_at)
                self.history["last_completed_duration_seconds"] = duration
                self.history["last_completed_duration_text"] = fmt_duration(duration)
            self.job_started_at = None
        elif self.prev_state is None and state in ("IDLE", "FAILED"):
            # First report after startup: remember the finished job if we
            # have nothing better.
            self.history.setdefault("last_completed_job_name", self.report.get("subtask_name") or None)
        self.prev_state = state
        self._save_history()

    def status_json(self):
        with self.lock:
            p = dict(self.report)
            online = time.time() - self.last_message_at < STALE_AFTER_SECS
            history = dict(self.history)

        state = normalize_state(p.get("gcode_state")) if online else "OFFLINE"
        active = state in ("PRINTING", "PAUSED", "PREPARING")
        layer, total_layers = p.get("layer_num"), p.get("total_layer_num")
        remaining_min = p.get("mc_remaining_time")

        printer = {
            "id": SERIAL,
            "name": NAME,
            "online": online,
            "state": state,
            "label": p.get("gcode_state"),
            "job_name": (p.get("subtask_name") or None) if active else history.get("last_completed_job_name"),
            "job_objects": f"Layer {layer}/{total_layers}" if active and total_layers else None,
            "progress": p.get("mc_percent") if active else None,
            "time_left": remaining_min * 60 if active and remaining_min is not None else None,
            "nozzle_temp": p.get("nozzle_temper"),
            "bed_temp": p.get("bed_temper"),
            "print_error": p.get("print_error"),
            **{k: history.get(k) for k in (
                "idle_since",
                "last_completed_job_name",
                "last_completed_end_time",
                "last_completed_duration_seconds",
                "last_completed_duration_text",
            )},
        }
        return {"updated_at": iso(time.time()), "printers": [printer]}


printer_state = PrinterState()


def request_pushall(client):
    client.publish(
        f"device/{SERIAL}/request",
        json.dumps({"pushing": {"sequence_id": "0", "command": "pushall"}}),
    )


def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code.is_failure:
        log.error("MQTT connect failed: %s", reason_code)
        return
    log.info("Connected to printer at %s", HOST)
    client.subscribe(f"device/{SERIAL}/report")
    request_pushall(client)


def on_disconnect(client, userdata, flags, reason_code, properties):
    log.warning("Disconnected from printer: %s", reason_code)


def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload)
    except ValueError:
        return
    fields = data.get("print")
    if isinstance(fields, dict):
        printer_state.update(fields)


class StatusHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] not in ("/", "/status.json"):
            self.send_error(404)
            return
        body = json.dumps(printer_state.status_json(), indent=2).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass  # the server polls often; keep the journal quiet


def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set("bblp", ACCESS_CODE)
    # The printer uses a self-signed certificate.
    client.tls_set(cert_reqs=ssl.CERT_NONE)
    client.tls_insecure_set(True)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=5, max_delay=60)
    # connect_async + loop_start retries in the background, so the bridge
    # starts fine even while the printer is powered off.
    client.connect_async(HOST, 8883, keepalive=60)
    client.loop_start()

    def pushall_loop():
        while True:
            time.sleep(PUSHALL_INTERVAL_SECS)
            if client.is_connected():
                request_pushall(client)

    threading.Thread(target=pushall_loop, daemon=True).start()

    server = ThreadingHTTPServer(("", PORT), StatusHandler)
    log.info("Serving status.json on port %d", PORT)
    server.serve_forever()


if __name__ == "__main__":
    main()
