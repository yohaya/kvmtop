#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef CMD_MAX
#define CMD_MAX 512
#endif

// --- Terminal Handling ---
static struct termios orig_termios;
static int raw_mode_enabled = 0;

static void disable_raw_mode() {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = 0;
        printf("\033[?25h"); 
    }
}

static void enable_raw_mode() {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled = 1;
    printf("\033[?25l"); 
}

static int wait_for_input(double seconds) {
    struct timeval tv;
    tv.tv_sec = (long)seconds;
    tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1e6);
    if (tv.tv_usec < 0) tv.tv_usec = 0;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return c;
    }
    return 0; // Timeout
}

// --- Data Structures ---
typedef struct {
    pid_t pid;  // This is the TID (Thread ID)
    pid_t tgid; // This is the Process ID (Thread Group ID)
    uint64_t key; // Unique key (usually just pid/tid)

    uint64_t syscr;
    uint64_t syscw;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t cpu_jiffies;
    uint64_t blkio_ticks;

    double cpu_pct;
    double r_iops;
    double w_iops;
    double io_wait_ms;
    double r_mib;
    double w_mib;

    char cmd[CMD_MAX];
} sample_t;

// ... (vec_t funcs) ...

static uint64_t make_key(pid_t tid) {
    return (uint64_t)tid; 
}

// ... (helpers) ...

static int collect_samples(vec_t *out, const pid_t *filter_pids, size_t filter_n) {
    DIR *proc = opendir("/proc");
    if (!proc) { perror("opendir(/proc)"); return -1; }
    struct dirent *de;
    
    while ((de = readdir(proc)) != NULL) {
        if (!is_numeric_str(de->d_name)) continue;
        pid_t pid = (pid_t)atoi(de->d_name); // This is the TGID
        
        // Filter by TGID (Process ID)
        if (filter_n > 0 && !pid_in_filter(pid, filter_pids, filter_n)) continue;

        char cmd[CMD_MAX];
        read_cmdline(pid, cmd);

        char taskdir_path[PATH_MAX];
        snprintf(taskdir_path, sizeof(taskdir_path), "/proc/%d/task", pid);
        DIR *taskdir = opendir(taskdir_path);
        
        if (taskdir) {
            struct dirent *te;
            while ((te = readdir(taskdir)) != NULL) {
                if (!is_numeric_str(te->d_name)) continue;
                pid_t tid = (pid_t)atoi(te->d_name);
                
                sample_t s; memset(&s, 0, sizeof(s));
                s.pid = tid; 
                s.tgid = pid;
                s.key = make_key(tid);
                snprintf(s.cmd, sizeof(s.cmd), "%s", cmd); // Threads share cmdline usually

                char io_path[PATH_MAX], stat_path[PATH_MAX];
                snprintf(io_path, sizeof(io_path), "/proc/%d/task/%d/io", pid, tid);
                snprintf(stat_path, sizeof(stat_path), "/proc/%d/task/%d/stat", pid, tid);
                
                read_io_file(io_path, &s.syscr, &s.syscw, &s.read_bytes, &s.write_bytes);
                read_proc_stat_fields(stat_path, &s.cpu_jiffies, &s.blkio_ticks);
                vec_push(out, &s);
            }
            closedir(taskdir);
        } else {
            // Fallback if task dir unreadable (should be rare)
            sample_t s; memset(&s, 0, sizeof(s));
            s.pid = pid; 
            s.tgid = pid;
            s.key = make_key(pid);
            snprintf(s.cmd, sizeof(s.cmd), "%s", cmd);

            char io_path[PATH_MAX], stat_path[PATH_MAX];
            snprintf(io_path, sizeof(io_path), "/proc/%d/io", pid);
            snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
            
            read_io_file(io_path, &s.syscr, &s.syscw, &s.read_bytes, &s.write_bytes);
            read_proc_stat_fields(stat_path, &s.cpu_jiffies, &s.blkio_ticks);
            vec_push(out, &s);
        }
    }
    closedir(proc);
    return 0;
}

static int cmp_key(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->key > y->key) - (x->key < y->key);
}

static const sample_t *find_prev(const vec_t *prev, uint64_t key) {
    size_t lo = 0, hi = prev->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint64_t k = prev->data[mid].key;
        if (k == key) return &prev->data[mid];
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

static int get_term_cols(void) {
    int cols = 120;
    if (isatty(STDOUT_FILENO)) {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) if (ws.ws_col > 0) cols = ws.ws_col;
    }
    return cols;
}

static void fprint_trunc(FILE *out, const char *s, int width) {
    if (width <= 0) return;
    int len = (int)strlen(s);
    if (len <= width) fprintf(out, "%-*s", width, s);
    else if (width <= 3) fprintf(out, "%.*s", width, s);
    else fprintf(out, "%.*s...", width - 3, s);
}

// Sort Comparators
typedef enum { SORT_PID=1, SORT_CPU, SORT_RIOPS, SORT_WIOPS, SORT_RMIB, SORT_WMIB } sort_col_t;

static int cmp_pid_desc(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->pid < y->pid) - (x->pid > y->pid); 
}
static int cmp_cpu_desc(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->cpu_pct < y->cpu_pct) - (x->cpu_pct > y->cpu_pct);
}
static int cmp_riops_desc(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->r_iops < y->r_iops) - (x->r_iops > y->r_iops);
}
static int cmp_wiops_desc(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->w_iops < y->w_iops) - (x->w_iops > y->w_iops);
}
static int cmp_rmib_desc(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->r_mib < y->r_mib) - (x->r_mib > y->r_mib);
}
static int cmp_wmib_desc(const void *a, const void *b) {
    const sample_t *x = (const sample_t *)a;
    const sample_t *y = (const sample_t *)b;
    return (x->w_mib < y->w_mib) - (x->w_mib > y->w_mib);
}

// Aggregate threads into process-level stats
static void aggregate_by_tgid(const vec_t *src, vec_t *dst) {
    vec_init(dst);
    // Src is sorted by KEY (TID), but we need to group by TGID.
    // Easiest is to push all, then sort by TGID, then merge.
    // But src is already deltas/metrics calculated. 
    
    // We need a map or simple sort-merge. Let's do sort-merge.
    // 1. Copy src to dst (shallow copy of data is bad if we modify, so deep copy)
    for (size_t i=0; i<src->len; i++) {
        vec_push(dst, &src->data[i]);
    }
    
    // 2. Sort by TGID
    // Helper comparator for TGID
    int cmp_tgid(const void *a, const void *b) {
        const sample_t *x = (const sample_t *)a;
        const sample_t *y = (const sample_t *)b;
        return (x->tgid > y->tgid) - (x->tgid < y->tgid);
    }
    qsort(dst->data, dst->len, sizeof(sample_t), cmp_tgid);

    // 3. Merge adjacent with same TGID
    size_t write_idx = 0;
    if (dst->len > 0) {
        for (size_t i = 1; i < dst->len; i++) {
            if (dst->data[write_idx].tgid == dst->data[i].tgid) {
                // Merge i into write_idx
                dst->data[write_idx].cpu_pct += dst->data[i].cpu_pct;
                dst->data[write_idx].r_iops += dst->data[i].r_iops;
                dst->data[write_idx].w_iops += dst->data[i].w_iops;
                dst->data[write_idx].io_wait_ms += dst->data[i].io_wait_ms;
                dst->data[write_idx].r_mib += dst->data[i].r_mib;
                dst->data[write_idx].w_mib += dst->data[i].w_mib;
                // Keep the PID of the TGID (usually the main thread) or just use TGID
                dst->data[write_idx].pid = dst->data[write_idx].tgid; 
            } else {
                write_idx++;
                dst->data[write_idx] = dst->data[i];
                dst->data[write_idx].pid = dst->data[i].tgid; // Ensure PID column shows TGID
            }
        }
        dst->len = write_idx + 1;
    }
}

// Tree view helper: Find all threads for a given TGID in the raw list
static void print_threads_for_tgid(const vec_t *raw, pid_t tgid, int cols, int pidw, int cpuw, int iopsw, int waitw, int mibw, int cmdw) {
    for (size_t i = 0; i < raw->len; i++) {
        const sample_t *s = &raw->data[i];
        if (s->tgid == tgid && s->pid != tgid) { // Don't print the main thread again if it matches TGID exactly, or do? 
            // Usually main thread has PID == TGID. The Process Row already covers the sum.
            // If we want to show breakdown, we should show ALL threads including main.
            // But visually, the "Process" row acts as the sum. 
            // Let's show all threads indented.
            
            char pidbuf[32];
            snprintf(pidbuf, sizeof(pidbuf), "  └─ %d", s->pid); // Indent
            
            printf("%*s %*.*f %*.*f %*.*f %*.*f %*.*f %*.*f ",
                pidw, pidbuf,
                cpuw, 2, s->cpu_pct,
                iopsw, 2, s->r_iops,
                iopsw, 2, s->w_iops,
                waitw, 2, s->io_wait_ms,
                mibw, 2, s->r_mib,
                mibw, 2, s->w_mib);
            fprint_trunc(stdout, s->cmd, cmdw);
            putchar('\n');
        }
    }
}

int main(int argc, char **argv) {
    if (geteuid() != 0) {
        fprintf(stderr, "Warning: Not running as root. IO stats will be unavailable for other users' processes.\n");
        sleep(2);
    }

    double interval = 5.0; 
    int display_limit = 50;
    int show_tree = 0; 
    
    pid_t *filter = NULL;
    size_t filter_n = 0, filter_cap = 0;

    static const struct option long_opts[] = {
        {"interval", required_argument, NULL, 'i'},
        {"pid", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:p:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'i': interval = strtod(optarg, NULL); if (interval <= 0) return 2; break;
            case 'p': {
                long v = strtol(optarg, NULL, 10);
                if (filter_n == filter_cap) {
                    filter_cap = filter_cap ? filter_cap * 2 : 8;
                    filter = realloc(filter, filter_cap * sizeof(*filter));
                }
                filter[filter_n++] = (pid_t)v;
                break;
            }
            case 'h': default: return 0;
        }
    }

    long hz = sysconf(_SC_CLK_TCK);
    vec_t prev, curr_raw, curr_proc;
    vec_init(&prev); vec_init(&curr_raw); vec_init(&curr_proc);
    
    printf("Initializing (wait %.0fs)...\n", interval);
    // Initial collection (always collect threads)
    if (collect_samples(&prev, filter, filter_n) != 0) return 1;
    qsort(prev.data, prev.len, sizeof(sample_t), cmp_key);
    double t_prev = now_monotonic();

    enable_raw_mode();
    sort_col_t sort_col = SORT_CPU;

    while (1) {
        vec_free(&curr_raw); vec_init(&curr_raw);
        if (collect_samples(&curr_raw, filter, filter_n) != 0) break;
        
        double t_curr = now_monotonic();
        double dt = t_curr - t_prev;
        if (dt <= 0) dt = interval;

        // 1. Calculate Metrics for ALL threads (Raw)
        for (size_t i=0; i<curr_raw.len; i++) {
            sample_t *c = &curr_raw.data[i];
            const sample_t *p = find_prev(&prev, c->key);
            uint64_t d_cpu=0, d_scr=0, d_scw=0, d_rb=0, d_wb=0, d_blk=0;
            if (p) {
                d_cpu = (c->cpu_jiffies >= p->cpu_jiffies) ? c->cpu_jiffies - p->cpu_jiffies : 0;
                d_scr = (c->syscr >= p->syscr) ? c->syscr - p->syscr : 0;
                d_scw = (c->syscw >= p->syscw) ? c->syscw - p->syscw : 0;
                d_rb  = (c->read_bytes >= p->read_bytes) ? c->read_bytes - p->read_bytes : 0;
                d_wb  = (c->write_bytes >= p->write_bytes) ? c->write_bytes - p->write_bytes : 0;
                d_blk = (c->blkio_ticks >= p->blkio_ticks) ? c->blkio_ticks - p->blkio_ticks : 0;
            }
            c->cpu_pct = ((double)d_cpu * 100.0) / (dt * (double)hz);
            c->r_iops = (double)d_scr / dt;
            c->w_iops = (double)d_scw / dt;
            c->r_mib  = ((double)d_rb / dt) / 1048576.0;
            c->w_mib  = ((double)d_wb / dt) / 1048576.0;
            c->io_wait_ms = ((double)d_blk * 1000.0) / (double)hz; 
        }

        // 2. Aggregate into Process List
        vec_free(&curr_proc); // Clear old
        aggregate_by_tgid(&curr_raw, &curr_proc);

        int dirty = 1;
        double start_wait = now_monotonic();

        while (1) {
            if (dirty) {
                vec_t *view_list = &curr_proc; // Default view is aggregated processes

                // Sort the PROCESS list
                switch(sort_col) {
                    case SORT_PID: qsort(view_list->data, view_list->len, sizeof(sample_t), cmp_pid_desc); break;
                    case SORT_CPU: qsort(view_list->data, view_list->len, sizeof(sample_t), cmp_cpu_desc); break;
                    case SORT_RIOPS: qsort(view_list->data, view_list->len, sizeof(sample_t), cmp_riops_desc); break;
                    case SORT_WIOPS: qsort(view_list->data, view_list->len, sizeof(sample_t), cmp_wiops_desc); break;
                    case SORT_RMIB: qsort(view_list->data, view_list->len, sizeof(sample_t), cmp_rmib_desc); break;
                    case SORT_WMIB: qsort(view_list->data, view_list->len, sizeof(sample_t), cmp_wmib_desc); break;
                }

                printf("\033[2J\033[H"); // Clear screen
                int cols = get_term_cols();
                
                printf("kvmtop - Refresh=%.1fs | Mode: %s ('t' to toggle)\n", interval, show_tree ? "Tree" : "List");

                int pidw = 14; // Wider for tree indentation
                int cpuw = 8, iopsw = 10, mibw = 10, waitw=10;
                int fixed = pidw+1 + cpuw+1 + iopsw+1 + iopsw+1 + waitw+1 + mibw+1 + mibw+1;
                int cmdw = cols - fixed; if (cmdw < 10) cmdw = 10;

                char h_pid[32], h_cpu[32], h_ri[32], h_wi[32], h_rm[32], h_wm[32], h_wt[32];
                snprintf(h_pid, 32, "[1] %s", "PID");
                snprintf(h_cpu, 32, "[2] %s", "CPU%%");
                snprintf(h_ri, 32, "[3] %s", "R_Sys");
                snprintf(h_wi, 32, "[4] %s", "W_Sys");
                snprintf(h_wt, 32, "[5] %s", "IO_Wait");
                snprintf(h_rm, 32, "[6] %s", "R_MiB/s");
                snprintf(h_wm, 32, "[7] %s", "W_MiB/s");

                printf("%*s %*s %*s %*s %*s %*s %*s ",
                    pidw, h_pid, cpuw, h_cpu, iopsw, h_ri, iopsw, h_wi, waitw, h_wt, mibw, h_rm, mibw, h_wm);
                fprint_trunc(stdout, "COMMAND", cmdw);
                putchar('\n');
                
                for(int i=0; i<cols; i++) putchar('-');
                putchar('\n');

                // Calculate Totals (from Raw to be accurate)
                double t_cpu=0, t_ri=0, t_wi=0, t_rm=0, t_wm=0, t_wt=0;
                for(size_t i=0; i<curr_raw.len; i++) {
                    t_cpu += curr_raw.data[i].cpu_pct;
                    t_ri  += curr_raw.data[i].r_iops;
                    t_wi  += curr_raw.data[i].w_iops;
                    t_rm  += curr_raw.data[i].r_mib;
                    t_wm  += curr_raw.data[i].w_mib;
                    t_wt  += curr_raw.data[i].io_wait_ms;
                }

                int limit = display_limit; 
                if ((size_t)limit > view_list->len) limit = view_list->len;
                
                for (int i=0; i<limit; i++) {
                    const sample_t *c = &view_list->data[i]; // 'c' is an aggregated PROCESS
                    
                    char pidbuf[32];
                    snprintf(pidbuf, sizeof(pidbuf), "%d", c->tgid);
                    
                    // Highlight process row in Tree mode?
                    printf("%*s %*.*f %*.*f %*.*f %*.*f %*.*f %*.*f ",
                        pidw, pidbuf,
                        cpuw, 2, c->cpu_pct,
                        iopsw, 2, c->r_iops,
                        iopsw, 2, c->w_iops,
                        waitw, 2, c->io_wait_ms,
                        mibw, 2, c->r_mib,
                        mibw, 2, c->w_mib);
                    fprint_trunc(stdout, c->cmd, cmdw);
                    putchar('\n');

                    if (show_tree) {
                        print_threads_for_tgid(&curr_raw, c->tgid, cols, pidw, cpuw, iopsw, waitw, mibw, cmdw);
                    }
                }

                for(int i=0; i<cols; i++) putchar('-');
                putchar('\n');
                printf("%*s %*.*f %*.*f %*.*f %*.*f %*.*f %*.*f \n",
                        pidw, "TOTAL",
                        cpuw, 2, t_cpu,
                        iopsw, 2, t_ri,
                        iopsw, 2, t_wi,
                        waitw, 2, t_wt,
                        mibw, 2, t_rm,
                        mibw, 2, t_wm);
                fflush(stdout);
                dirty = 0;
            }

            double elapsed = now_monotonic() - start_wait;
            double remain = interval - elapsed;
            if (remain <= 0) break;

            int c = wait_for_input(remain);
            if (c > 0) {
                if (c == 'q' || c == 'Q') goto cleanup;
                if (c == 't' || c == 'T') { show_tree = !show_tree; dirty = 1; } // Toggle Tree
                if (c == '1' || c == 0x01) { sort_col = SORT_PID; dirty = 1; }
                if (c == '2' || c == 0x02) { sort_col = SORT_CPU; dirty = 1; }
                if (c == '3' || c == 0x03) { sort_col = SORT_RIOPS; dirty = 1; }
                if (c == '4' || c == 0x04) { sort_col = SORT_WIOPS; dirty = 1; }
                if (c == '5' || c == 0x05) { sort_col = SORT_RMIB; dirty = 1; }
                if (c == '6' || c == 0x06) { sort_col = SORT_WMIB; dirty = 1; }
            } else {
                break;
            }
        }

        // Prepare for next frame: PREV gets the current RAW data
        qsort(curr_raw.data, curr_raw.len, sizeof(sample_t), cmp_key);
        vec_free(&prev); 
        // Deep copy curr_raw to prev or just swap?
        // We can just swap pointers, but we need to re-init curr_raw.
        // vec_t prev = curr_raw; NO, memory management.
        // Easier:
        prev = curr_raw; // Transfer ownership of data
        vec_init(&curr_raw); // Reset curr for next loop
        t_prev = t_curr;
    }

cleanup:
    disable_raw_mode();
    vec_free(&prev);
    vec_free(&curr_raw);
    vec_free(&curr_proc);
    free(filter);
    return 0;
}
