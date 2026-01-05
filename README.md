# kvmtop

**kvmtop** is a specialized, lightweight, real-time monitoring tool designed for Linux servers hosting KVM (Kernel-based Virtual Machine) guests (e.g., Proxmox VE, plain Libvirt/QEMU). 

It bridges the visibility gap by automatically correlating low-level system processes with high-level Virtual Machine identities.

![Process View](https://placeholder-for-screenshot-process-view.png)

## 🚀 Key Features

1.  **Auto-Discovery:** Parsing `/proc` to instantly map PIDs and TAP interfaces to VMIDs and Names.
2.  **Zero Dependencies:** Running as a single, static binary. No Python, no Libvirt libraries, no installation.
3.  **Latency Focus:** Highlighting `IO_Wait` to instantly spot storage bottlenecks affecting specific VMs.
4.  **Network Visibility:** Dedicated view for VM network traffic, auto-associated with the correct VM.

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
| **CPU%** | CPU Usage | Real-time CPU usage. Can exceed 100% for multi-threaded processes (1 core = 100%). |
| **R_Log / W_Log** | **Logical** Read/Write IOPS | The number of `read()` or `write()` system calls the application made per second. *High R_Log + Low R_MiB/s = Excellent RAM Caching.* |
| **IO_Wait** | **Latency** (ms) | Total milliseconds the process spent **blocked** waiting for physical Disk I/O. **This is your primary bottleneck indicator.** High values (>1000ms) mean the storage array is too slow for the VM. |
| **R_MiB/s** | Read Bandwidth | Physical data read from the storage layer (excluding page cache hits). |
| **W_MiB/s** | Write Bandwidth | Physical data written to the storage layer. |
| **COMMAND** | Context | Shows the VMID and Name (e.g., `100 - database-vm`) if identified, or the process name. |

---

### 2. Network View (Press `n`)
Switches to a view focusing on network interfaces, specifically tuned for virtualization.

*   **Auto-Filtering:** Automatically hides host firewall interfaces (`fw*`, `tap*i*`) and loopback to reduce noise.
*   **Top 50:** Shows the top 50 busiest interfaces sorted by Transmit rate (default).
*   **VM Mapping:** Matches `tap` interfaces (e.g., `tap105i0`) to their owning VM (e.g., `105 - webserver`).

| Column | Explanation |
| :--- | :--- |
| **IFACE** | Interface Name (e.g., `eth0`, `tap100i0`). |
| **STATE** | Link state (UP/DOWN). |
| **RX_Mbps** | Receive rate in Megabits per second. |
| **TX_Mbps** | Transmit rate in Megabits per second. |
| **RX/TX_Pkts** | Packets per second (PPS). High PPS with low Mbps indicates small packet storm (DDoS/DNS). |
| **VMID / NAME** | The ID and Name of the VM this interface belongs to. |

---

### 3. Tree View (Press `t`)
Toggles a tree visualization in the Process View.
*   Shows the main QEMU process.
*   Lists all worker threads (vCPUs, IO threads) indented beneath it.
*   Useful for identifying if a single vCPU is pegged at 100% inside a large VM.

## ⌨️ Keyboard Shortcuts

| Key | Function |
| :--- | :--- |
| **`n`** | **Network Mode:** Toggle between Process View and Network View. |
| **`t`** | **Tree Mode:** Toggle thread tree visualization. |
| **`f`** | **Freeze:** Pause/Resume display updates (useful to copy text). |
| **`1`** | **Sort by PID** (or RX in Net mode). |
| **`2`** | **Sort by CPU** (or TX in Net mode). |
| **`3`** | **Sort by Read Logs**. |
| **`4`** | **Sort by Write Logs**. |
| **`5`** | **Sort by IO Wait** (Latency). |
| **`6`** | **Sort by Read Bandwidth**. |
| **`7`** | **Sort by Write Bandwidth**. |
| **`q`** | **Quit**. |

## 🛠️ Troubleshooting

**Q: Why do I see 0.00 for R_MiB/s and IO_Wait?**
**A:** You are likely running as a non-root user. The Linux kernel restricts access to `/proc/[pid]/io` (where these stats live) to the root user for security. Please run with `sudo`.

**Q: I don't see VM names, just command lines.**
**A:** `kvmtop` parses standard QEMU/KVM command lines (Proxmox style). If you are using a custom wrapper or different libvirt naming convention, the parser might miss the name. Feel free to open an issue with your process command line structure.

## 📜 License

This project is licensed under the **GNU General Public License v3.0**.
