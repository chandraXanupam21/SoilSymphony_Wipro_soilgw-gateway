#!/usr/bin/env bash
# Real kernel-driver test. Needs: root, linux-headers-$(uname -r), build-essential.
set -euo pipefail
cd "$(dirname "$0")/.."
[ "$(id -u)" -eq 0 ] || { echo "run as root: sudo $0"; exit 1; }
make -s all && make -s driver
insmod driver/soilgw.ko
trap 'kill $DPID 2>/dev/null || true; rmmod soilgw' EXIT
ls -l /dev/soilgw; cat /proc/soilgw
./build/soilgwd -c config/soilgw.conf &
DPID=$!
sleep 0.5
# The simulator writes raw bytes into the driver; the driver validates/queues
# frames; the daemon reads them back with poll()/read().
./build/soilgw-sim -o /dev/soilgw -n 6 -i 300 -t 8 --start 30 --decay 1.5 --corrupt 0.05 --garbage 0.05 >/dev/null
./build/soilgw-cli status
./build/soilgw-cli stats      # includes kernel.* counters read via ioctl
echo "--- /proc/soilgw ---"; cat /proc/soilgw
dmesg | tail -3
