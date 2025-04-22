# Tronberry

A daemon to fetch images from a Tronbyt server and display them on an LED matrix connected to a Raspberry Pi.

Tested with a Raspberry Pi Zero 2W, but it should work on any Pi model supported by [hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix).

---

## 🚫 Flicker Fix First

Before you get started, you'll want to disable audio and isolate a CPU core to avoid display flickering (instructions borrowed from [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix?tab=readme-ov-file#troubleshooting)):

1. Edit `/boot/firmware/config.txt` and set:

    ```
    dtparam=audio=off
    ```

2. Edit `/boot/firmware/cmdline.txt` and append:

    ```
    isolcpus=3
    ```

3. Run these commands:

    ```sh
    cat <<EOF | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
    blacklist snd_bcm2835
    EOF
    sudo update-initramfs -u
    ```

4. Reboot.

---

## 🚀 One-Line Install (Recommended)

This script handles everything: dependencies, compilation, service setup, boot configuration — and it’ll even ask you a few questions.

```sh
wget https://raw.githubusercontent.com/joeadops/tronberry/main/setup-tronberry.sh && chmod +x setup-tronberry.sh && ./setup-tronberry.sh myusername
```

🔧 Replace `myusername` with your actual username on the Pi.  
(`pi` is common, but some of you out there like to get fancy.)

---

## 🔨 Manual Install

Want to do it yourself? You rebel. Here's how:

```sh
# Install dependencies
sudo apt-get update
sudo apt-get install -y git libwebp-dev libssl-dev zlib1g-dev

# Clone the repo
git clone --recurse-submodules https://github.com/joeadops/tronberry.git
cd tronberry

# Build the binary
make release
```

---

### 🔧 Create the config file

```sh
nano tronberry.conf
```

Inside `tronberry.conf`, add:

```ini
URL=http://192.168.68.42:8000/d8e59932/next
```

You can change this later to point to any Tronbyt server.

---

## ▶️ Running Tronberry

To run manually:

```sh
sudo ./tronberry tronberry.conf
```

---

## 🧞‍♂️ Auto-Start on Boot

Create `/etc/systemd/system/tronberry.service` with the following contents:

```ini
[Unit]
Description=Tronberry LED Matrix Client
After=network-online.target

[Service]
ExecStart=/home/pi/tronberry/tronberry /home/pi/tronberry/tronberry.conf
Restart=always
User=pi
WorkingDirectory=/home/pi/tronberry

[Install]
WantedBy=multi-user.target
```

Replace `/home/pi/` with your actual username if different.

Then run:

```sh
sudo systemctl enable tronberry
sudo systemctl start tronberry
```

To check on it later:

```sh
sudo systemctl status tronberry
```

---

## 🧠 Notes

- You can change the image source anytime by editing `tronberry.conf`
- The app supports static and animated WebP images
- Transitions are customizable and include wipes, pulses, orbiting loaders, and more!

---

That’s it — happy glowing! 🌈🕶️
