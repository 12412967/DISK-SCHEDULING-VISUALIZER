/*
 * C-SCAN Disk Scheduling Algorithm (Circular SCAN)
 * Moves head in one direction servicing requests, jumps back to the
 * opposite end WITHOUT servicing on the return, then sweeps again.
 *
 * Usage: ./cscan <initial_head> <disk_size> <direction> <queue>
 * Example: ./cscan 50 200 1 82,170,43,140
 *
 * direction: 0 = moving left (toward 0), 1 = moving right (toward disk_size-1)
 *
 * Output: JSON { "sequence": [...], "seek_time": N }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        if (*end != '\0')                return -1;  /* non-numeric   */
        if (val < 0 || val >= disk_size) return -1;  /* out of range  */

        arr[count++] = (int)val;
        tok = strtok(NULL, ",");
    }
    return count;
}

/* ---------- comparison for qsort ---------- */

static int cmp_int(const void *a, const void *b) {
    return (*(int *)a - *(int *)b);
}

/* ---------- C-SCAN core ---------- */

/*
 * C-SCAN (Circular SCAN):
 *
 * Moving RIGHT (direction == 1):
 *   1. Service all requests >= head  in ascending  order.
 *   2. Jump to cylinder 0 (no service on the way back).
 *   3. Service all requests <  head  in ascending  order.
 *
 * Moving LEFT (direction == 0):
 *   1. Service all requests <= head  in descending order.
 *   2. Jump to cylinder disk_size-1  (no service on the way back).
 *   3. Service all requests >  head  in descending order.
 *
 * The jump cost (boundary → opposite end) IS counted in seek_time
 * because the physical head still traverses that distance.
 *
 * out_seq[] records every stop including both boundary positions
 * so the frontend can animate the full movement faithfully.
 *
 * Returns total seek distance.
 */
static int cscan(int head, int disk_size, int direction,
                 int *requests, int n,
                 int *out_seq, int *out_len) {

    /* Split requests into those < head and those >= head */
    int lower[MAX_REQUESTS], lower_cnt = 0;   /* cylinders <  head */
    int upper[MAX_REQUESTS], upper_cnt = 0;   /* cylinders >= head */

    for (int i = 0; i < n; i++) {
        if (requests[i] < head)
            lower[lower_cnt++] = requests[i];
        else
            upper[upper_cnt++] = requests[i];
    }

    /* Both groups sorted ascending; we will reverse lower for LEFT sweeps */
    qsort(lower, lower_cnt, sizeof(int), cmp_int);
    qsort(upper, upper_cnt, sizeof(int), cmp_int);

    int total = 0;
    int pos   = head;
    int idx   = 0;

    out_seq[idx++] = pos;   /* record initial head position */

    if (direction == 1) {
        /* ── RIGHT sweep ─────────────────────────────────────────── */

        /* Step 1: service upper (>= head) in ascending order */
        for (int i = 0; i < upper_cnt; i++) {
            total += abs(upper[i] - pos);
            pos    = upper[i];
            out_seq[idx++] = pos;
        }

        /* Step 2: travel to the RIGHT boundary (disk_size - 1) */
        if (pos != disk_size - 1) {
            total += abs((disk_size - 1) - pos);
            pos    = disk_size - 1;
            out_seq[idx++] = pos;          /* boundary marker */
        }

        /* Step 3: jump (wrap) to cylinder 0 — cost counted, recorded */
        if (lower_cnt > 0) {               /* only jump if there's work left */
            total += (disk_size - 1);      /* full span from end to start    */
            pos    = 0;
            out_seq[idx++] = pos;          /* wrap-around marker             */

            /* Step 4: service lower (< head) in ascending order */
            for (int i = 0; i < lower_cnt; i++) {
                total += abs(lower[i] - pos);
                pos    = lower[i];
                out_seq[idx++] = pos;
            }
        }

    } else {
        /* ── LEFT sweep ──────────────────────────────────────────── */

        /* Step 1: service lower (< head) in descending order */
        for (int i = lower_cnt - 1; i >= 0; i--) {
            total += abs(lower[i] - pos);
            pos    = lower[i];
            out_seq[idx++] = pos;
        }

        /* Step 2: travel to the LEFT boundary (cylinder 0) */
        if (pos != 0) {
            total += pos;                  /* distance to 0 */
            pos    = 0;
            out_seq[idx++] = pos;          /* boundary marker */
        }

        /* Step 3: jump (wrap) to disk_size-1 — cost counted, recorded */
        if (upper_cnt > 0) {              /* only jump if there's work left */
            total += (disk_size - 1);     /* full span from start to end    */
            pos    = disk_size - 1;
            out_seq[idx++] = pos;         /* wrap-around marker             */

            /* Step 4: service upper (>= head) in descending order */
            for (int i = upper_cnt - 1; i >= 0; i--) {
                total += abs(upper[i] - pos);
                pos    = upper[i];
                out_seq[idx++] = pos;
            }
        }
    }

    *out_len = idx;
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
        print_error("Usage: ./cscan <head> <disk_size> <direction> <queue>");
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

    /* --- Parse direction --- */
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
        /* Empty queue: no movement */
        int seq[1] = { (int)head };
        print_result(seq, 1, 0);
        return 0;
    }

    /* --- Run C-SCAN --- */
    /*
     * Maximum sequence length:
     *   1  (initial head)
     * + upper_cnt  (right-side requests)
     * + 1  (right boundary)
     * + 1  (cylinder 0 after wrap)
     * + lower_cnt  (left-side requests)
     * = n + 3  (worst case)
     */
    int out_seq[MAX_REQUESTS + 4];
    int seq_len  = 0;
    int seek_time = cscan((int)head, (int)disk_size, (int)direction,
                          requests, n, out_seq, &seq_len);

    print_result(out_seq, seq_len, seek_time);
    return 0;
}