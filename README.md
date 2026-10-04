# Smart Agricultural Soil-Moisture Multi-Node Gateway

> **Individual Project - Linux Device Drivers, System Programming & C++**

| | |
|---|---|
| **Student** | `<Anupam Chandra>` |
| **Roll / ID** | `<2341003015>` |
| **Course / Batch** | `<B.tech CSE>` |
| **Teacher / Mentor** | `<Santosh K Dhanpal>` |
| **Platform** | Linux (developed on CachyOS (Arch)) |
| **Languages** | C (kernel driver), C++17 (daemon and tools), Bash (scripts) |

---

## 1. What is this project?

Farmers often water crops on a fixed schedule or by guesswork. This wastes water and can hurt the crop,
because soil moisture is different in different parts of a field.

This project is a **gateway** that runs on a Linux computer (for example a Raspberry Pi). It:

1. receives soil-moisture readings from **many sensor nodes**,
2. checks that the data is **valid** (corrupt data is rejected),
3. **smooths** the readings and watches every node's health,
4. raises **alerts** such as *"Zone 3 moisture 22% is below 25% - irrigation needed"*,
5. **saves** all readings and alerts to log files and lets you view them live from a command-line tool.

No physical hardware is needed to try it: a **node simulator** produces realistic sensor data,
including broken sensors, silent nodes and corrupted messages.

## 2. How it works (architecture)

```
 Sensor nodes 1..N  (or the simulator)
        |  8-byte data frames
        v
 +--------------------------+
 | KERNEL DRIVER (C)        |   /dev/soilgw
 | checks CRC, ring buffer, |   /proc/soilgw
 | poll(), ioctl()          |
 +------------+-------------+
              |  read() / poll()
              v
 +--------------------------+
 | GATEWAY DAEMON (C++)     |   soilgwd
 | reader thread  (epoll)   |
 | processor thread         |-----> logs/readings.csv, logs/alerts.log
 | control-socket thread    |
 +------------+-------------+
              |  UNIX socket
              v
        soilgw-cli   (status / alerts / stats)
```

| Layer | What it does | Main files |
|---|---|---|
| Kernel driver | Receives bytes, finds frame start, checks CRC-8, stores frames in a 256-slot ring buffer, supports blocking and non-blocking `read`, `poll`, `ioctl`, `/proc` | `driver/soilgw.c` |
| System layer | Threads, `epoll`, signals, UNIX socket, daemon mode, file I/O | `daemon/src/gateway.cpp`, `main.cpp` |
| Application layer | Node state machine, moving average, alert thresholds with hysteresis, configuration, logging | `daemon/src/node.cpp`, `node_manager.cpp`, `config.cpp`, `logger.cpp` |
| Tools | Node simulator and control CLI | `tools/sim.cpp`, `tools/cli.cpp` |

The full design, UML class / sequence / state diagrams are in [`docs/03_design.md`](docs/03_design.md).

## 3. Features

- Handles **up to 32 nodes** at the same time (nodes appear automatically when their first frame arrives).
- **Data validation** with CRC-8 in the kernel driver and again in the daemon.
- **Node health state machine:** `UNKNOWN -> ONLINE -> OFFLINE / FAULTY -> ONLINE`.
- **Lost-frame detection** using sequence numbers.
- **Moving-average filter** so one noisy reading does not trigger a false alert.
- **Low / high moisture alerts with hysteresis** (no alert flooding near a threshold).
- **Per-node thresholds** in a config file (for example a drought-tolerant crop in zone 3).
- **Live reload** of the config with `kill -HUP` and **graceful shutdown** with `kill -TERM`.
- Can run as a normal program or a **background daemon** (`--daemon`).
- Tested with unit tests, an end-to-end test, ThreadSanitizer and AddressSanitizer.

## 4. Requirements

| System | Install command |
|---|---|
| CachyOS / Arch | `sudo pacman -S --needed base-devel git` |
| Ubuntu / Debian | `sudo apt install build-essential git` |

Only for the kernel driver (optional): the kernel headers matching your running kernel.

## 5. Build and run (no kernel module needed)

```bash
git clone https://github.com/<YOUR-GITHUB-NAME>/soilgw-gateway.git
cd soilgw-gateway

make                  # build everything into ./build
make test             # 25 unit tests
make integration      # full end-to-end test
./scripts/run_demo.sh # live demo (press Ctrl+C to stop)
```

The demo uses a named pipe (`/tmp/soilgw.fifo`) as a **virtual device**, so the real driver is not required.

### Example output of the live demo / `soilgw-cli status`

```
NODE  STATE    MOIST(%)  AVG(%)   BATT(mV) FRAMES  LOST   LEVEL   LAST SEEN
1     ONLINE   58.68     31.11    4089     45      0      NORMAL  0.7s ago
2     ONLINE   13.65     17.51    4082     25      0      LOW     0.7s ago
3     ONLINE   17.46     21.11    4075     43      2      NORMAL  0.6s ago
4     ONLINE   17.58     21.11    4068     43      2      LOW     0.6s ago
```

### Example alerts (`soilgw-cli alerts` or `logs/alerts.log`)

```
[LOW_MOISTURE]   Zone 2 moisture 24.4% is below 25% - irrigation needed
[MOISTURE_NORMAL] Zone 3 moisture back to normal (31.0%)
[NODE_FAULTY]    Node 3 faulty: 3 consecutive invalid readings
[NODE_OFFLINE]   Node 2 offline (no data for > 2000 ms)
[NODE_RECOVERED] Node 2 recovered
```

## 6. Using the programs

**Start the daemon** (virtual device mode):

```bash
mkfifo /tmp/soilgw.fifo
./build/soilgwd -c config/demo.conf
```

**Start the simulator** in a second terminal:

```bash
./build/soilgw-sim -o /tmp/soilgw.fifo -n 6 -i 600 -t 60 --start 40 --decay 0.5 \
                   --offline 2:8:20 --faulty 4:12:22 --corrupt 0.03 --garbage 0.03
```

**Look at the data** in a third terminal:

```bash
./build/soilgw-cli status      # table of all nodes
./build/soilgw-cli alerts      # recent alerts
./build/soilgw-cli stats       # counters (frames, errors, queue)
./build/soilgw-cli -w status   # live view, refreshes every second
```

| Simulator option | Meaning |
|---|---|
| `-o PATH` | where to send data (`/dev/soilgw` or a FIFO) |
| `-n N` | number of nodes (1-32) |
| `-i MS` | time between rounds in milliseconds |
| `-t S` | run for S seconds (0 = forever) |
| `--offline N:S:E` | node N is silent from second S to E |
| `--faulty N:S:E` | node N reports invalid values from second S to E |
| `--corrupt P` / `--garbage P` | probability of corrupted frames / noise bytes |

**Configuration** is in [`config/soilgw.conf`](config/soilgw.conf): thresholds, averaging window,
offline timeout, per-node overrides.

## 7. Using the real kernel driver (optional)

```bash
sudo pacman -S linux-cachyos-lts-headers clang llvm lld   # CachyOS; use linux-headers-$(uname -r) on Ubuntu
make driver LLVM=1        # on Ubuntu: make driver
sudo ./scripts/test_driver.sh
```

> **Note:** the driver code is written for Linux 5.x - 6.x. It has to be compiled against your own kernel.
> If Secure Boot is on, unsigned modules will not load.

## 8. Frame format

Every sensor message is 8 bytes:

| Byte | 0 | 1 | 2 | 3-4 | 5-6 | 7 |
|---|---|---|---|---|---|---|
| Meaning | `0xAA` start | node id (1-32) | sequence number | moisture, 0.01 % units | battery in mV | CRC-8 of bytes 1-6 |

## 9. Testing

| Level | Command | Result |
|---|---|---|
| Unit tests | `make test` | 25 tests, 85 checks, all pass |
| End-to-end | `make integration` | 15 checks pass (alerts, offline, faulty, logging, shutdown) |
| Sanitizers | see [`docs/05_testing.md`](docs/05_testing.md) | clean under ThreadSanitizer and ASan/UBSan |
| Kernel driver | `sudo ./scripts/test_driver.sh` | procedure in `docs/05_testing.md` |

## 10. Project documentation (6 stages)

| Stage | Document |
|---|---|
| 1 Introduction | [`docs/01_introduction.md`](docs/01_introduction.md) |
| 2 Requirements (PRD) and plan | [`docs/02_PRD.md`](docs/02_PRD.md) |
| 3 Design, architecture, UML, Git strategy | [`docs/03_design.md`](docs/03_design.md) |
| 4 Prototype and progress log | [`docs/04_progress_log.md`](docs/04_progress_log.md) |
| 5 Testing and improvement | [`docs/05_testing.md`](docs/05_testing.md) |
| 6 Final report, limitations, future work | [`docs/06_final_report.md`](docs/06_final_report.md) |

## 11. Folder structure

```
soilgw/
|-- include/soilgw_proto.h   shared frame format, CRC, ioctl numbers
|-- driver/                  Linux kernel module (soilgw.c)
|-- daemon/src/              gateway daemon (C++17)
|-- tools/                   simulator (sim.cpp) and CLI (cli.cpp)
|-- tests/                   unit tests
|-- scripts/                 demo, integration test, driver test, git setup
|-- config/                  configuration files
|-- docs/                    documents for all 6 stages
|-- Makefile
`-- README.md
```

## 12. Git workflow

- `main` = stable versions, tagged at the end of each stage (`v0.4-stage4`, `v0.5-stage5`, `v1.0`).
- `develop` = daily integration branch.
- `feature/<name>` = one branch per new piece of work.
- Commit messages follow `type(scope): message`, for example `feat(node): add alert cooldown`.

## 13. Limitations and future work

**Limitations:** the driver receives frames through `write()` and is not yet connected to a real UART/RS-485
interrupt path; the control socket is protected only by file permissions; logs are not rotated automatically.

**Future work:** pump/valve control, SQLite storage, cloud dashboard (MQTT), LoRaWAN nodes, sensor calibration,
real UART support with device tree, ML irrigation prediction, systemd service file.

## 14. License / Author

Written as an academic individual project by `<YOUR NAME>`.
