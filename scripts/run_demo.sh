#!/usr/bin/env bash
# Run the whole system without the kernel module: FIFO acts as the virtual device.
set -euo pipefail
cd "$(dirname "$0")/.."
make -s all
FIFO=/tmp/soilgw.fifo
[ -p "$FIFO" ] || mkfifo "$FIFO"
./build/soilgwd -c config/demo.conf &
DPID=$!
trap 'kill $DPID 2>/dev/null; kill $SPID 2>/dev/null; wait 2>/dev/null; rm -f $FIFO' EXIT
sleep 0.5
# 6 nodes, node 2 silent 8s..20s, node 4 faulty 12s..22s, occasional corrupt frames/noise
./build/soilgw-sim -o "$FIFO" -n 6 -i 600 -t 60 --start 40 --decay 0.5 \
    --offline 2:8:20 --faulty 4:12:22 --corrupt 0.03 --garbage 0.03 >/dev/null &
SPID=$!
echo "Live view (Ctrl-C to quit). Alerts are in ./logs/alerts.log"
./build/soilgw-cli -w status
