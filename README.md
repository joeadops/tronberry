# Tronberry

A daemon to fetch images from a Tronbyt server and display on a matrix LED display connected to a Raspberry Pi.

It has been tested with a Raspberry Pi Zero 2W and a Raspberry Pi 4 / CM4, and should work with all devices supported by https://github.com/hzeller/rpi-rgb-led-matrix.

## About this fork

This fork tracks [tronbyt/tronberry](https://github.com/tronbyt/tronberry) and adds animated transitions between apps:

- **Orbit dots**: four rainbow dots orbit the center of the panel.
- **Pulse**: rainbow rings expand and contract from the center.

By default the two alternate. Choose one, or turn them off, with `--transition=alternate|orbit|pulse|none`.

It also adds `--black-level=N`, which draws any pixel whose brightest channel is N or less as black. Many apps use near-black backgrounds, such as dark navy, that look black on a monitor but glow faintly on an LED panel. The installer defaults to `--black-level=40`.

The installer defaults to a 64x32 panel on an Adafruit RGB Matrix HAT (`--led-gpio-mapping=adafruit-hat --led-slowdown-gpio=4`). A Pi 4 or CM4 drives the GPIO pins too fast for most panels at lower slowdown values, which shows up as sparkly static around lit pixels.

### Running the server on the same Pi

The [Tronbyt server](https://github.com/tronbyt/server) can run on the same Pi as the display. A Pi 4 handles both easily. Download the `tronbyt-server-linux-arm64` binary from its releases, run it as a systemd service, and point tronberry at `ws://localhost:8000/<device-id>/ws`.

## Installation

Run the installation script and follow the instructions. The script will ask whether you want to apply optimizations to prevent LED matrix flickering. Note that this will disable audio and Bluetooth and requires a reboot.

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/joeadops/tronberry/HEAD/install.sh)"
```

## Configuration

The installer asks for additional command line flags which can be used to match the connected LED matrix display. The documentation at https://github.com/hzeller/rpi-rgb-led-matrix documents the available flags.

If you missed this during the initial installation, you can either run the installer again or add the flags to the `ExecStart` parameter in `/etc/systemd/system/tronberry.service`.

## Upgrading

To upgrade to a new version, just run the install script again:

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/joeadops/tronberry/HEAD/install.sh)"
```

## Building instructions

If you want to build the Tronbyt client yourself on the device:

```sh
# Install dependencies
sudo apt-get update
sudo apt-get -y install git libwebp-dev libssl-dev zlib1g-dev

# Clone repository
git clone --recurse-submodules https://github.com/joeadops/tronberry.git

# Build
cd tronberry
make
```

Compilation on the device is slow and might run out of memory, so you may consider cross-compiling the binary on
a more powerful machine using `docker build -f Dockerfile.cross --output . .`.

## Running

For normal operations, the service should be run as a systemd service using `sudo systemctl enable`.

For testing, you can also run it manually (as root):

```sh
# The Tronbyt URL looks like http(s)://…/next or ws(s)://…/ws
sudo ./tronberry ${TRONBYT_URL}
```

If you use `tronberry` with the original Tidbyt display, add the `--led-panel-type=FM6126A` flag. For a list of available options, run `./tronberry --help`, there are many knobs to tweak.
