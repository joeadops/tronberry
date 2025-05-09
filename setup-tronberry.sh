#!/bin/bash
set -e

# Parse optional username argument
TARGET_USER=${1:-pi}
HOME_DIR=$(eval echo "~$TARGET_USER")
INSTALL_DIR="$HOME_DIR/tronberry"

HARDWARE ?= regular

echo "Using user: $TARGET_USER"
echo "Install path: $INSTALL_DIR"

echo "Installing required packages..."
sudo apt update
sudo apt install -y git make g++ build-essential cmake libssl-dev libwebp-dev zlib1g-dev

echo "Cloning or updating tronberry repo..."
cd "$HOME_DIR"

if [ -d "$INSTALL_DIR/.git" ]; then
  echo "Repo exists, pulling latest changes..."
  cd "$INSTALL_DIR"
  git pull origin main || git pull origin master
  git submodule update --init --recursive
else
  echo "Cloning fresh copy..."
  git clone https://github.com/joeadops/tronberry.git
  cd tronberry
  git submodule update --init --recursive
fi

echo "Configure LED Matrix dimensions..."

read -p "Enter LED matrix width [64]: " MATRIX_WIDTH
MATRIX_WIDTH=${MATRIX_WIDTH:-64}

read -p "Enter LED matrix height [32]: " MATRIX_HEIGHT
MATRIX_HEIGHT=${MATRIX_HEIGHT:-32}

read -p "Enter hardware type [adafruit-hat]: " HARDWARE_TYPE
HARDWARE_TYPE=${HARDWARE_TYPE:-adafruit-hat}

export MATRIX_WIDTH
export MATRIX_HEIGHT
export HARDWARE_TYPE

echo "Configure Tronbyt server  URL..."

read -p "Enter Tronbyt server URL [http://192.168.68.42:8000/d8e59932/next]: " TRONBYT_URL
TRONBYT_URL=${TRONBYT_URL:-http://192.168.68.42:8000/d8e59932/next}

CONF_FILE="$INSTALL_DIR/tronberry.conf"

echo "Writing config to $CONF_FILE..."
cat > "$CONF_FILE" <<EOL
URL=$TRONBYT_URL
EOL

echo "Config file created."
echo "   You can edit the URL later by running:"
echo "   nano $CONF_FILE"

echo "Using dimensions: ${MATRIX_WIDTH}x${MATRIX_HEIGHT}"
echo "Using hardware: ${HARDWARE_TYPE}"
echo "Building tronberry..."
make release HARDWARE=$HARDWARE_TYPE

echo "Creating tronberry.service..."
SERVICE_FILE=/etc/systemd/system/tronberry.service

sudo bash -c "cat > $SERVICE_FILE" <<EOL
[Unit]
Description=Tronberry LED Matrix Service
After=network.target

[Service]
ExecStart=$INSTALL_DIR/tronberry $INSTALL_DIR/tronberry.conf
WorkingDirectory=$INSTALL_DIR
StandardOutput=inherit
StandardError=inherit
Restart=always
User=$TARGET_USER

[Install]
WantedBy=multi-user.target
EOL

echo "Enabling and starting the service..."
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable tronberry
sudo systemctl restart tronberry

echo "Tronberry is installed and running as $TARGET_USER!"
echo "Use 'sudo systemctl status tronberry' to check status."

# Prompt for Wi-Fi SSID to disable autoconnect-retries
read -rp "Enter your Wi-Fi SSID (case-sensitive) to enable auto-reconnect on disconnect: " WIFI_SSID
if [ -n "$WIFI_SSID" ]; then
  echo "Applying autoconnect-retries=0 for Wi-Fi network: $WIFI_SSID"
  sudo nmcli connection modify "$WIFI_SSID" connection.autoconnect-retries 0
else
  echo "No SSID entered. Skipping Wi-Fi autoconnect tweak."
fi

echo " ////////////////////////////////////////////////////////////////////////////////"
echo "###############################################################################.,"
echo "#.............................................................................#.,"
echo "#.............................................................................#.,"
echo "#████████╗██████╗..██████╗.███╗...██╗██████╗.███████╗██████╗.██████╗.██╗...██╗#.,"
echo "#╚══██╔══╝██╔══██╗██╔═══██╗████╗..██║██╔══██╗██╔════╝██╔══██╗██╔══██╗╚██╗ ██╔╝#.,"
echo "#...██║...██████╔╝██║...██║██╔██╗.██║██████╔╝█████╗..██████╔╝██████╔╝.╚████╔╝.#.,"
echo "#...██║...██╔══██╗██║...██║██║╚██╗██║██╔══██╗██╔══╝..██╔══██╗██╔══██╗..╚██╔╝..#.,"
echo "#...██║...██║..██║╚██████╔╝██║.╚████║██████╔╝███████╗██║..██║██║..██║...██║...#.,"
echo "#...╚═╝...╚═╝  ╚═╝ ╚═════╝.╚═╝..╚═══╝╚═════╝.╚══════╝╚═╝..╚═╝╚═╝..╚═╝...╚═╝...#.,"
echo "#.............................................................................#.,"
echo "#.............................................................................#.,"
echo "#............................INSTALL COMPLETE.................................#.,"
echo "#.............................................................................#.,"
echo "###############################################################################. "
