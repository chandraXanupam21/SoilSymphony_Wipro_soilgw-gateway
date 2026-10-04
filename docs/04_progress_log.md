# Stage 4 - Initial Implementation & Prototype: Progress Log

Add one row per working session and commit it together with the code. Entries below record what the
delivered code contains and the problems solved while building it.

| Step | Work done | Evidence |
|---|---|---|
| 1 | Shared protocol header, CRC-8, frame format | `include/soilgw_proto.h` |
| 2 | Kernel driver: cdev, ring buffer, wait queue, poll, ioctl, /proc | `driver/soilgw.c` |
| 3 | `FrameParser` + unit tests | `tests/test_main.cpp` parser_* |
| 4 | Node state machine, moving average, hysteresis alerts | node_*, low_alert_* tests |
| 5 | Gateway: reader/processor/IPC threads, epoll, eventfd, UNIX socket | `daemon/src/gateway.cpp` |
| 6 | Simulator, CLI, CSV/alert logging | `tools/`, `logs/` |
| 7 | End-to-end demo | `scripts/run_demo.sh` |

## Prototype demonstration
`./scripts/run_demo.sh` shows 6 simulated nodes with live status, a node going offline (8-20 s), a faulty sensor
(12-22 s), corrupt frames and line noise, and low-moisture alerts.

## Issues and solutions
| Issue | Cause | Solution |
|---|---|---|
| Daemon busy-looped when the simulator exited | epoll reports EPOLLHUP on a FIFO with no writer | daemon opens FIFO `O_RDWR` so it is also a writer and never sees EOF |
| Alert storm while moisture hovered near threshold | no hysteresis | `Thresholds::hysteresis`; clear only at low+hyst / high-hyst |
| Single noisy sample triggered irrigation alert | raw readings used directly | moving-average window (default 5) |
| Corrupt frame swallowed the next good frame | parser dropped the whole 8 bytes on CRC failure | on bad CRC keep bytes after the next SOF (test `parser_recovers_after_truncated_frame`) |
| Invalid sensor values (655.35 %) skewed the average | no validation | validity check + FAULTY after 3 consecutive invalid readings; invalid data never enters the filter |
| Signal handler cannot take locks | async-signal-safety | handler only sets an atomic and writes an `eventfd`; threads poll it |
| `copy_to_user` may sleep | cannot be called under spinlock | frames copied into a stack buffer under the lock, user copy after unlock |

## Next stage roadmap (Stage 5)
Unit/integration/system tests, sanitizers, performance measurements, load test with 32 nodes, code-quality pass.
