/*
 * FCFS Disk Scheduling Algorithm
 * First Come First Served - processes requests in arrival order
 *
 * Usage: ./fcfs <initial_head> <disk_size> <direction> <queue>
 * Example: ./fcfs 50 200 0 82,170,43,140
 *
 * Output: JSON { "sequence": [...], "seek_time": N }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REQUESTS 256

/* ---------- helpers ---------- */

static void print_error(const char *msg) {
    /* Safe fallback: zero seek time, empty sequence */
    fprintf(stderr, "Error: %s\n", msg);
    printf("{\"sequence\": [], \"seek_time\": 0, \"error\": \"%s\"}\n", msg);
}

/*
 * Parse comma-separated integer list into arr[].
 * Returns count of parsed values, or -1 on failure.
 */
static int parse_queue(const char *str, int *arr, int max, int disk_size) {
    if (!str || strlen(str) == 0) return 0;

    char buf[4096];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int count = 0;
    char *tok = strtok(buf, ",");
    while (tok) {
        if (count >= max) {
            return -1; /* too many requests */
        }
        char *end;
        long val = strtol(tok, &end, 10);
        if (*end != '\0') return -1;            /* non-numeric token */
        if (val < 0 || val >= disk_size) return -1; /* out of range */
        arr[count++] = (int)val;
        tok = strtok(NULL, ",");
    }
    return count;
}

/* ---------- FCFS core ---------- */

/*
 * FCFS: visit requests exactly in the order given.
 * Fills out_seq[] with the traversal path (including initial head).
 * Returns total seek distance.
 */
static int fcfs(int head, int *requests, int n, int *out_seq) {
    int total = 0;
    int pos   = head;
    int idx   = 0;

    out_seq[idx++] = pos;               /* starting position */

    for (int i = 0; i < n; i++) {
        int dist = abs(requests[i] - pos);
        total   += dist;
        pos      = requests[i];
        out_seq[idx++] = pos;
    }

    return total;
}

/* ---------- JSON output ---------- */

static void print_result(int *seq, int seq_len, int seek_time) {
    printf("{\"sequence\": [");
    for (int i = 0; i < seq_len; i++) {
        printf("%d", seq[i]);
        if (i < seq_len - 1) printf(", ");
    }
    printf("], \"seek_time\": %d}\n", seek_time);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    /* Expect exactly 4 arguments after program name */
    if (argc != 5) {
        print_error("Usage: ./fcfs <head> <disk_size> <direction> <queue>");
        return 1;
    }

    /* --- Parse initial head --- */
    char *end;
    long head = strtol(argv[1], &end, 10);
    if (*end != '\0' || head < 0) {
        print_error("Invalid initial head position");
        return 1;
    }

    /* --- Parse disk size --- */
    long disk_size = strtol(argv[2], &end, 10);
    if (*end != '\0' || disk_size <= 0) {
        print_error("Invalid disk size");
        return 1;
    }

    /* --- Validate head within disk --- */
    if (head >= disk_size) {
        print_error("Head position exceeds disk size");
        return 1;
    }

    /* --- Parse direction (unused in FCFS but validated for consistency) --- */
    long direction = strtol(argv[3], &end, 10);
    if (*end != '\0' || (direction != 0 && direction != 1)) {
        print_error("Direction must be 0 (left) or 1 (right)");
        return 1;
    }

    /* --- Parse request queue --- */
    int requests[MAX_REQUESTS];
    int n = parse_queue(argv[4], requests, MAX_REQUESTS, (int)disk_size);
    if (n < 0) {
        print_error("Invalid request queue (out-of-range or non-numeric values)");
        return 1;
    }
    if (n == 0) {
        /* Empty queue: head stays, zero movement */
        int seq[1] = { (int)head };
        print_result(seq, 1, 0);
        return 0;
    }

    /* --- Run FCFS --- */
    int out_seq[MAX_REQUESTS + 1];
    int seek_time = fcfs((int)head, requests, n, out_seq);

    print_result(out_seq, n + 1, seek_time);
    return 0;
}