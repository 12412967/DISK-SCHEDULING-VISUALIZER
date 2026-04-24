/*
 * SSTF Disk Scheduling Algorithm
 * Shortest Seek Time First - always services the closest request next
 *
 * Usage: ./sstf <initial_head> <disk_size> <direction> <queue>
 * Example: ./sstf 50 200 0 82,170,43,140
 *
 * Output: JSON { "sequence": [...], "seek_time": N }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX_REQUESTS 256

/* ---------- helpers ---------- */

static void print_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    printf("{\"sequence\": [], \"seek_time\": 0, \"error\": \"%s\"}\n", msg);
}

/*
 * Parse comma-separated integer list into arr[].
 * Returns count, or -1 on failure.
 */
static int parse_queue(const char *str, int *arr, int max, int disk_size) {
    if (!str || strlen(str) == 0) return 0;

    char buf[4096];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int count = 0;
    char *tok = strtok(buf, ",");
    while (tok) {
        if (count >= max) return -1;

        char *end;
        long val = strtol(tok, &end, 10);
        if (*end != '\0')          return -1;   /* non-numeric */
        if (val < 0 || val >= disk_size) return -1;  /* out of range */

        arr[count++] = (int)val;
        tok = strtok(NULL, ",");
    }
    return count;
}

/* ---------- SSTF core ---------- */

/*
 * SSTF: at each step, find the unvisited request with the smallest
 * absolute distance from the current head position and service it.
 *
 * visited[] tracks which requests have already been serviced.
 * out_seq[] receives the full traversal path (head + all requests).
 * Returns total seek distance.
 */
static int sstf(int head, int *requests, int n, int *out_seq) {
    int visited[MAX_REQUESTS] = {0};   /* 0 = pending, 1 = done */
    int total   = 0;
    int pos     = head;
    int idx     = 0;

    out_seq[idx++] = pos;              /* record starting position */

    for (int step = 0; step < n; step++) {

        /* Find the nearest unvisited request */
        int best_idx  = -1;
        int best_dist = INT_MAX;

        for (int i = 0; i < n; i++) {
            if (visited[i]) continue;
            int dist = abs(requests[i] - pos);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx  = i;
            }
            /* Tie-breaking: prefer lower cylinder (matches most textbook
               implementations; keeps output deterministic) */
            if (dist == best_dist && requests[i] < requests[best_idx]) {
                best_idx = i;
            }
        }

        if (best_idx == -1) break;     /* safety – should never happen */

        visited[best_idx]  = 1;
        total             += best_dist;
        pos                = requests[best_idx];
        out_seq[idx++]     = pos;
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

    if (argc != 5) {
        print_error("Usage: ./sstf <head> <disk_size> <direction> <queue>");
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

    /* --- Parse direction (unused in SSTF but validated for CLI consistency) --- */
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
        int seq[1] = { (int)head };
        print_result(seq, 1, 0);
        return 0;
    }

    /* --- Run SSTF --- */
    int out_seq[MAX_REQUESTS + 1];
    int seek_time = sstf((int)head, requests, n, out_seq);

    print_result(out_seq, n + 1, seek_time);
    return 0;
}