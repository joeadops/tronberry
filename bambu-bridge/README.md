# Bambu bridge

Serves a Bambu Lab printer's status as `status.json` for the Tronbyt **Bambu Printer Status** app (`bambuprinterstatus`).

It reads the printer's local MQTT feed over your LAN using the printer's access code. It doesn't use a Bambu cloud account, and nothing leaves your network. Progress, time remaining and layer counts come straight from the printer and update every few seconds.

## Install (on the Pi)

```sh
sudo apt-get install -y python3-paho-mqtt
mkdir -p ~/bambu-bridge
cp bambu_bridge.py ~/bambu-bridge/
```

Create `~/bambu-bridge/.env` with values from the printer's screen:

```ini
# Settings > WLAN (reserve this IP in your router)
BAMBU_HOST=192.168.1.50
# Settings > Device
BAMBU_SERIAL=01P00A000000000
# Settings > WLAN > Access Code
BAMBU_ACCESS_CODE=12345678
BAMBU_NAME=P1S
```

systemd doesn't support comments at the end of a line, so keep each comment on its own line.

Run `chmod 600 ~/bambu-bridge/.env`, then install `bambu-bridge.service` into `/etc/systemd/system/` and run `sudo systemctl enable --now bambu-bridge`.

Check that it works with `curl http://localhost:8787/status.json`.

## App setup

In the Tronbyt web UI, add **Bambu Printer Status** and set the Status JSON URL to `http://localhost:8787/status.json`. Use that address when the bridge runs on the same Pi as the server; otherwise use the Pi's IP address.

Turn on **Only During Active Prints** if you want the app to show up in the rotation only while something is printing.

## Notes

- **States:** the printer's states map to the app's like this: RUNNING → PRINTING, PAUSE → PAUSED, PREPARE/SLICING → PREPARING, FAILED → FAILED, and anything else → IDLE. If no report arrives for 60 seconds, it shows OFFLINE.
- **Job history:** the last finished job's name and duration are saved in `bridge_state.json`, so they survive restarts.
- **Firmware restrictions:** some printer firmware restricts third-party LAN access. If the bridge logs `Not authorized` with a correct access code, try enabling LAN Only or Developer Mode on the printer.
