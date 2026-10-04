# Stage 1 - Project Introduction

**Project:** Smart Agricultural Soil-Moisture Multi-Node Gateway

## Idea and objective
A Linux gateway that collects soil-moisture readings from several field nodes, validates and
processes them, and turns them into actionable irrigation alerts. The stack has three layers:
kernel driver (`/dev/soilgw`), POSIX system daemon, and a C++17 application layer.

## Problem
- Fixed-schedule irrigation wastes water and harms yield; moisture varies across a field, so many nodes are needed.
- Sensor data is noisy; nodes go offline, send corrupt frames, or fail.
- Low-cost gateways need a lightweight, reliable Linux solution.

**Problem to solve:** reliably collect, validate and manage data from multiple nodes and raise alerts such as
*"Zone 3 moisture 22% is below 25% - irrigation needed"*.

## Scope
**In:** char driver (ring buffer, blocking/non-blocking read, poll, ioctl, /proc) - C++ daemon (frame parsing, CRC,
node state tracking, filtering, thresholds/alerts, CSV logging, control CLI) - node simulator - tests, docs, UML.
**Out (future work):** pump/valve hardware, cloud dashboard, mobile app, LoRaWAN, ML prediction.

## Expected outcome and application
4-32 nodes handled concurrently; live per-node status and alerts; persistent logs; detection of node timeout,
corrupt frames and faulty sensors with automatic recovery. Applications: precision agriculture, greenhouses,
research plots, and teaching the full kernel-to-application path.

## Skills demonstrated
| Course area | Where |
|---|---|
| Device drivers | cdev, file_operations, spinlocks, wait queues, poll, ioctl, procfs, class/device |
| System programming | threads, mutex/condvar, epoll, eventfd, UNIX sockets, signals, daemonisation, file I/O |
| C++ | RAII (`UniqueFd`), templates (`MovingAverage<T>`, `BoundedQueue<T>`), STL, smart pointers, state machine |
