# kvmtop

> **The missing `top` for Linux KVM hypervisors.**

**kvmtop** is a specialized, lightweight, real-time monitoring tool designed for Linux servers hosting KVM (Kernel-based Virtual Machine) guests (e.g., Proxmox VE, plain Libvirt/QEMU).

It bridges the visibility gap between standard system tools (like `htop`, `iotop`) and hypervisor-specific data, automatically correlating low-level system processes with high-level Virtual Machine identities.

![Process View](https://placeholder-for-screenshot-process-view.png)

## 🚀 Why kvmtop?

Standard tools often fail to provide clear context in a virtualization environment:
*   **`top`/`htop`**: Show `qemu-kvm` processes but don't tell you *which* VM they belong to (unless you manually parse long command lines). They also don't show per-process Disk I/O latency.
*   **`iotop`**: Shows disk usage but is heavy (Python-based) and doesn't map threads or tap interfaces to VMs.
*   **`virt-top`**: Requires libvirt, is often slow, and misses raw physical network interface statistics.

**kvmtop solves this by:**
1.  **Auto-Discovery:** Parsing `/proc` to instantly map PIDs and TAP interfaces to VMIDs and Names.
2.  **Zero Dependencies:** Running as a single, static binary. No Python, no Libvirt libraries, no installation.
3.  **Latency Focus:** Highlighting `IO_Wait` to instantly spot storage bottlenecks affecting specific VMs.

## 📦 Installation

### Quick Start (Pre-compiled Binary)
The easiest way to run `kvmtop` is to download the latest static binary. It works on **any** x86_64 Linux distribution (Debian, Ubuntu, CentOS, RHEL, Alpine, Proxmox).

```bash
# Download and run in one step
curl -L -o kvmtop https://github.com/yohaya/kvmtop/releases/download/latest/kvmtop-static-linux-amd64
chmod +x kvmtop
sudo ./kvmtop
```

*(Note: `sudo` is highly recommended to view Disk I/O statistics, which are restricted by the kernel to root users.)*

### Building from Source
If you prefer to build it yourself:

1.  **Prerequisites:** `gcc`, `make`.
2.  **Build:**
    ```bash
    git clone https://github.com/yohaya/kvmtop.git
    cd kvmtop
    make
    sudo ./build/kvmtop
    ```

## 🖥️ Usage & Interface

### 1. Process View (Default)
This is the main dashboard. It focuses on CPU and Disk performance per process/VM.

| Column | Metric | Detailed Explanation |
| :--- | :--- | :--- |
| **PID** | Process ID | The OS Process ID. For KVM, this is the main QEMU process. |
| **CPU