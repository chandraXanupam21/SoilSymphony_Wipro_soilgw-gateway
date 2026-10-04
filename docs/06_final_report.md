# Stage 6 - Final Report and Presentation Outline

## 1. Summary
A three-layer soil-moisture gateway: Linux character driver, multi-threaded POSIX daemon, C++17 application logic,
node simulator, CLI, tests and documentation. Requirements FR-1..FR-16 are implemented; user-space verified by
25 unit tests, 15 integration checks and sanitizer runs.

## 2. Achievements
- End-to-end data path with validation at two levels (kernel and daemon).
- Robust to corruption, line noise, node loss, sensor faults, slow consumers, config errors.
- Runtime control: SIGHUP reload, graceful shutdown, daemon mode, live CLI.
- Reproducible without hardware (FIFO virtual device or loopback via `/dev/soilgw` write path).

## 3. Limitations
- Real UART/RS-485 receive path is not wired up (driver ingests via `write()`; hook `parse_byte()` into a serial/tty
  line discipline or platform driver ISR for hardware).
- Kernel module requires manual compile/test on the target kernel version (version `#ifdef`s included for 5.x-6.x).
- Single device instance; no authentication on the control socket (filesystem permissions only).
- Logs rotate manually; no database back-end.
- Moisture is treated as a calibrated percentage; no per-sensor calibration curve.

## 4. Future improvements
Pump/valve control with schedule rules; SQLite storage and log rotation; MQTT/cloud dashboard; LoRaWAN or Modbus
nodes; sensor calibration; device-tree/platform-driver support with a real UART IRQ path and timers;
ML-based irrigation prediction; systemd unit and Debian package; GoogleTest migration.

## 5. Presentation outline (10-12 slides)
1 Title - 2 Problem - 3 Objectives and scope - 4 Architecture - 5 Driver design (ring buffer, FSM, ioctl, poll) -
6 Daemon design (threads, epoll, eventfd) - 7 C++ design (classes, state machine) - 8 UML (class, sequence, state) -
9 Demo (`run_demo.sh`: offline node, faulty node, alerts) - 10 Testing (unit, integration, sanitizers) -
11 Results, limitations, future work - 12 Q&A.

## 6. Submission checklist
- [ ] Source code and Git repository (tags per stage)  - [ ] PRD (`02_PRD.md`)  - [ ] Design + UML (`03_design.md`)
- [ ] Progress log  - [ ] Test report with your measured numbers  - [ ] Final report/slides  - [ ] Demo rehearsed
