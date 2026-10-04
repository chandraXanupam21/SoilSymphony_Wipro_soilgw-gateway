# Smart Agricultural Soil-Moisture Multi-Node Gateway

A Linux gateway stack for collecting soil-moisture readings from many field nodes:

| Layer | Technology | Location |
|---|---|---|
| Kernel | Character device driver (C) - ring buffer, wait queues, `poll`, `ioctl`, `/proc` | `driver/` |
| System | POSIX daemon - threads, `epoll`, UNIX sockets, signals, daemonisation, file I/O | `daemon/src/gateway.cpp`, `main.cpp` |
| Application | C++17 - node state machines, filters, alert engine, config, logging | `daemon/src/` |
| Tools | Node simulator (fault injection) and control CLI | `tools/` |

```
 Nodes 1..N  --(UART/RS-485 or simulator)-->  /dev/soilgw (kernel driver)
                                                     | read / poll / ioctl
                                              soilgwd (C++ daemon)
                                                     | UNIX socket
                                              soilgw-cli, logs/readings.csv, logs/alerts.log
```

## Quick start (no kernel module needed)

```bash
sudo apt install build-essential          # g++ >= 8, make
make                                       # builds soilgwd, soilgw-sim, soilgw-cli, run_tests
make test                                  # 25 unit/component tests
make integration                           # end-to-end test (daemon + simulator + CLI)
./scripts/run_demo.sh                      # live dashboard with faults injected
```

In demo mode a named pipe (`/tmp/soilgw.fifo`) acts as the virtual device.

## With the real kernel driver

```bash
sudo apt install linux-headers-$(uname -r)
sudo ./scripts/test_driver.sh              # builds + loads the module, runs the whole stack on /dev/soilgw
```
Manual: `make driver && sudo insmod driver/soilgw.ko`, then `./build/soilgwd -c config/soilgw.conf`,
and feed it with `./build/soilgw-sim -o /dev/soilgw`. Inspect with `cat /proc/soilgw`.

## Commands

```bash
./build/soilgwd -c config/soilgw.conf [-d DEV] [-s SOCK] [-l LOGDIR] [--daemon]
./build/soilgw-cli status | alerts | stats     # -w for live refresh, -s SOCK for a custom socket
./build/soilgw-sim -o DEV -n 6 -i 500 -t 60 --offline 2:10:20 --faulty 4:15:25 --corrupt 0.05 --garbage 0.05
kill -HUP  $(pidof soilgwd)    # reload thresholds without restart
kill -TERM $(pidof soilgwd)    # graceful stop
```

## Frame format (8 bytes)

`0xAA | node | seq | moisture(u16 BE, 0.01 %) | battery_mV(u16 BE) | CRC-8(poly 0x07, bytes 1..6)`

## Repository layout

```
include/soilgw_proto.h   shared kernel/user frame format, ioctl numbers, CRC-8
driver/                  kernel module (soilgw.c) + Kbuild Makefile
daemon/src/              protocol, node, node_manager, config, logger, gateway, main
tools/                   sim.cpp (node simulator), cli.cpp (control client)
tests/test_main.cpp      unit + component tests (no external framework)
scripts/                 run_demo, integration_test, test_driver, git_init
config/                  soilgw.conf (real device), demo.conf (FIFO)
docs/                    stage 1-6 documents, PRD, UML, test report
```

## Status

See `docs/04_progress_log.md` and `docs/05_testing.md`. The kernel module could not be
compiled in the authoring environment (no kernel headers); everything user-space is built and
tested (unit, integration, ThreadSanitizer, AddressSanitizer/UBSan).
