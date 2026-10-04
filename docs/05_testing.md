# Stage 5 - Testing, Integration & Improvement

## 1. Test levels
| Level | Tool | Command | Count |
|---|---|---|---|
| Unit | `tests/test_main.cpp` (dependency-free) | `make test` | 25 tests, 85 checks |
| Integration | `scripts/integration_test.sh` (daemon + simulator + CLI + files) | `make integration` | 15 checks |
| System / driver | `scripts/test_driver.sh` (needs root + kernel headers) | `sudo ./scripts/test_driver.sh` | manual |
| Sanitizers | ThreadSanitizer, AddressSanitizer + UBSan | see below | whole suite |

## 2. Unit test coverage
Protocol: CRC detection, round-trip, corrupt frame, garbage skipping, split frames, back-to-back frames, truncated frame
recovery, invalid node id. Filter: moving-average window. Node: UNKNOWN->ONLINE, timeout/recovery, FAULTY/recovery,
invalid streak reset, sequence loss with wraparound. Alerts: single alert + hysteresis, high alert, message content,
smoothing of one-sample dips. Config: parsing, overrides, 6 invalid-input cases. Manager: 8 independent nodes,
selective timeout, per-node thresholds, live reconfiguration. Queue: drop-when-full, non-blocking producer.

## 3. Integration scenario (automatic)
4 nodes at 5 Hz for 9 s, decay forcing low-moisture alerts, node 2 silent 2-6 s, node 3 faulty 3-6 s, 5 % corrupt frames,
5 % line noise. Assertions: nodes registered, >100 frames, corrupt frames counted, LOW_MOISTURE / NODE_OFFLINE /
NODE_FAULTY / NODE_RECOVERED alerts, CSV and alert logs written, zero queue drops, SIGHUP reload survives, SIGTERM exits
with code 0 and removes the socket.

## 4. Sanitizer runs
```bash
make clean && make CXXFLAGS="-std=c++17 -g -O1 -pthread -fsanitize=thread"          && make test && make integration
make clean && make CXXFLAGS="-std=c++17 -g -O1 -pthread -fsanitize=address,undefined" && make test && make integration
```
Result recorded during development: 25/25 unit tests and 15/15 integration checks pass under both; no data races,
leaks, or undefined behaviour reported.

## 5. Driver test procedure (TS-DRV)
| # | Step | Expected |
|---|---|---|
| 1 | `insmod driver/soilgw.ko`; `ls -l /dev/soilgw`; `cat /proc/soilgw` | node exists, counters 0 |
| 2 | `printf '\xaa\x01\x00\x0f\xa0\x0f\xa0\x??' > /dev/soilgw` (valid CRC) | `frames_ok` = 1, `queued` = 1 |
| 3 | `head -c 8 /dev/soilgw \| xxd` | the same 8 bytes |
| 4 | read on empty device in a blocking `cat` | blocks until the simulator writes |
| 5 | `python3 -c "import os;os.read(os.open('/dev/soilgw',os.O_NONBLOCK),8)"` on empty | `EAGAIN` |
| 6 | write garbage + a bad-CRC frame | `resync_bytes` / `frames_bad` increase |
| 7 | write > 256 frames with no reader | `overruns` increases, no crash |
| 8 | `soilgw-cli stats` | `kernel.*` counters match `/proc/soilgw` |
| 9 | `rmmod soilgw` while daemon stopped | clean unload, no oops in `dmesg` |

## 6. Improvements made
Hysteresis, averaging, bounded-drain loop in the reader (fairness), non-blocking producer queue, lock-free signal
path via eventfd, defensive config validation with line numbers, atomic stats for cross-thread reads.

## 7. Performance measurement (how to repeat)
```bash
./build/soilgwd -c config/demo.conf & ./build/soilgw-sim -o /tmp/soilgw.fifo -n 32 -i 32 -t 30 >/dev/null
./build/soilgw-cli stats   # frames_ok / uptime_s = frames per second; queue_dropped must stay 0
```
(-i 32 with 32 nodes = 1000 frames/s, the NFR-2 target.) Record the measured numbers in the final report.
