#!/usr/bin/env bash
# End-to-end test: daemon + simulator + CLI over a FIFO virtual device.
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); FIFO=$T/dev.fifo; SOCK=$T/gw.sock; LOGS=$T/logs
mkfifo "$FIFO"
pass=0; fail=0
check() { if eval "$2"; then echo "  PASS  $1"; pass=$((pass+1)); else echo "  FAIL  $1"; fail=$((fail+1)); fi; }

./build/soilgwd -c config/demo.conf -d "$FIFO" -s "$SOCK" -l "$LOGS" 2>"$T/daemon.err" &
DPID=$!
sleep 0.5
check "daemon is running" "kill -0 $DPID"
check "control socket created" "[ -S $SOCK ]"

# 4 nodes @ 200ms; node 2 silent 2-6s; node 3 faulty 3-6s; corruption + noise injected.
./build/soilgw-sim -o "$FIFO" -n 4 -i 200 -t 9 --start 30 --decay 1.5 --seed 7 \
    --offline 2:2:6 --faulty 3:3:6 --corrupt 0.05 --garbage 0.05 >/dev/null
sleep 0.5

STATUS=$(./build/soilgw-cli -s "$SOCK" status)
ALERTS=$(./build/soilgw-cli -s "$SOCK" alerts)
STATS=$(./build/soilgw-cli -s "$SOCK" stats)
echo "$STATUS"; echo "$ALERTS" | head -12; echo "$STATS"

check "all 4 nodes registered"            "[ \$(echo \"\$STATUS\" | tail -n +2 | wc -l) -eq 4 ]"
check "frames received (>100)"            "[ \$(echo \"\$STATS\" | awk '/frames_ok/{print \$2}') -gt 100 ]"
check "corrupt frames detected"           "[ \$(echo \"\$STATS\" | awk '/frames_bad/{print \$2}') -ge 1 ]"
check "low-moisture alert raised"         "echo \"\$ALERTS\" | grep -q LOW_MOISTURE"
check "node offline alert raised"         "echo \"\$ALERTS\" | grep -q 'NODE_OFFLINE'"
check "node recovered alert raised"       "echo \"\$ALERTS\" | grep -q 'NODE_RECOVERED'"
check "faulty-node alert raised"          "echo \"\$ALERTS\" | grep -q 'NODE_FAULTY'"
check "readings.csv written"              "[ \$(wc -l < $LOGS/readings.csv) -gt 100 ]"
check "alerts.log written"                "[ -s $LOGS/alerts.log ]"
check "no queue drops"                    "echo \"\$STATS\" | grep -q 'queue_dropped:   0'"

kill -HUP $DPID; sleep 0.3
check "survives SIGHUP reload"            "kill -0 $DPID"
kill -TERM $DPID; wait $DPID; RC=$?
check "graceful shutdown (exit code 0)"   "[ $RC -eq 0 ]"
check "socket removed on shutdown"        "[ ! -S $SOCK ]"

rm -rf "$T"
echo; echo "integration: $pass passed, $fail failed"
[ $fail -eq 0 ]
