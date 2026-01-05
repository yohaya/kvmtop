# kvmtop

**kvmtop** is a lightweight, high-performance, real-time monitoring tool designed specifically for Linux KVM (Kernel-based Virtual Machine) hosts.

## Key Features

*   **Truly Portable:** Distributed as a single, statically linked binary.
*   **No Install Required:** Runs on any modern Linux distribution.
*   **Lightweight:** Minimal resource footprint.
*   **KVM Mapping:** Automatically associates low-level processes and network interfaces with VMIDs and VM Names.
*   **IO Latency Tracking:** Tracks IO_Wait per process.

## Installation & Usage

```bash
curl -L -o kvmtop https://github.com/yohaya/kvmtop/releases/download/latest/kvmtop-static-linux-amd64 && chmod +x kvmtop && ./kvmtop
```

## Usage
Run as **root** to see full Disk I/O statistics.

```bash
sudo ./kvmtop
```

## Shortcuts
[n] Network View | [t] Tree View | [f] Freeze | [1-7] Sort | [q] Quit
