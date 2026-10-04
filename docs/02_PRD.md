# Stage 2 - Project Requirements Document (PRD)

## 1. Purpose
Define what the Soil-Moisture Gateway must do, how well, and how the work is planned.

## 2. Functional requirements
| ID | Requirement | Module | Verified by |
|---|---|---|---|
| FR-1 | Driver exposes `/dev/soilgw`; `read()` returns whole 8-byte frames | driver | test_driver.sh |
| FR-2 | Driver supports blocking and `O_NONBLOCK` reads and `poll()` | driver | test_driver.sh |
| FR-3 | Driver validates SOF + CRC-8 + node id, resynchronises after garbage | driver | parser tests, TS-DRV |
| FR-4 | Driver buffers frames in a 256-frame ring; oldest dropped on overrun and counted | driver | test_driver.sh |
| FR-5 | ioctl: get stats, clear, set/get node mask; `/proc/soilgw` shows counters | driver | test_driver.sh |
| FR-6 | Daemon reads device with `epoll`, re-validates frames, handles split/merged frames | daemon | UT parser_*, IT |
| FR-7 | Support at least 8 concurrent nodes (up to 32), created on first frame | NodeManager | UT manager_*, IT |
| FR-8 | Per-node state machine UNKNOWN/ONLINE/OFFLINE/FAULTY with timeout and recovery | Node | UT node_* |
| FR-9 | Moving-average filtering over a configurable window; invalid data never enters the filter | Node | UT |
| FR-10 | Per-node low/high thresholds with hysteresis; alert raised once per excursion | Node | UT low_alert_*, high_* |
| FR-11 | Detect lost frames from sequence gaps (with 8-bit wraparound) | Node | UT |
| FR-12 | Log every reading to CSV and every alert to a text log | Logger | IT |
| FR-13 | CLI commands `status`, `alerts`, `stats` (with live `-w` mode) via UNIX socket | Gateway, CLI | IT |
| FR-14 | Configuration file with per-node overrides; reload on SIGHUP without restart | Config | UT, IT |
| FR-15 | Run as foreground process or daemon (`--daemon`), graceful stop on SIGINT/SIGTERM | main | IT |
| FR-16 | Node simulator with offline, faulty, corruption and noise injection | tools | IT |

## 3. Non-functional requirements
| ID | Requirement | Target |
|---|---|---|
| NFR-1 | Reliability | No crash on malformed input; bad frames counted, never fatal |
| NFR-2 | Performance | Sustain >= 1000 frames/s on a Raspberry Pi class CPU; reader never blocks on slow consumers |
| NFR-3 | Latency | Alert raised within 100 ms of the triggering frame being processed; offline detection within timeout + 100 ms |
| NFR-4 | Resource use | < 10 MB RSS, idle CPU ~0 (event driven, no busy loops) |
| NFR-5 | Portability | Linux 5.x/6.x kernel, g++ >= 8 (C++17) |
| NFR-6 | Maintainability | Layered modules, no globals except signal hook, unit-testable (time injected) |
| NFR-7 | Safety | Clean under ThreadSanitizer, AddressSanitizer, UBSan |
| NFR-8 | Usability | Single config file; human-readable CLI output |

## 4. Scope, modules, deliverables
Modules: **M1** kernel driver - **M2** protocol (shared CRC/framing) - **M3** Node + state machine - **M4** NodeManager -
**M5** Config - **M6** Logger - **M7** Gateway (threads, epoll, IPC) - **M8** CLI - **M9** simulator - **M10** tests/scripts.

Deliverables: source code, PRD, design + UML, Git history, test report, final report/presentation.
Out of scope: pump control hardware, cloud, mobile, LoRaWAN, ML.

## 5. Assumptions and constraints
Single gateway per field; node link delivers bytes in order; development without hardware via simulator;
real UART/RS-485 hookup is a driver extension (same `parse_byte()` entry point).

## 6. Risks
| Risk | Mitigation |
|---|---|
| No kernel headers / risk of kernel panic during dev | develop in a VM; FIFO virtual device allows full user-space testing without the module |
| Concurrency bugs | few shared structures, one mutex each, TSan runs |
| Scope creep | fixed scope list above |

## 7. Development plan and timeline (6 weeks)
| Week | Stage | Output |
|---|---|---|
| 1 | 1-2 | Introduction, PRD, repo created |
| 2 | 3 | Architecture, UML, environment, Git branching |
| 3 | 4 | Driver + protocol + parser; simulator; daemon skeleton (prototype demo) |
| 4 | 4-5 | Node state machine, alerts, logging, CLI |
| 5 | 5 | Tests, sanitizers, bug fixing, performance |
| 6 | 6 | Final integration, report, presentation |
